#!/usr/bin/env python3
"""Đóng vai đài để thử tab "Điều khiển": nhận lệnh, trả trạng thái.

Công cụ nghe trên cổng lệnh (dòng "Command" trong bảng cổng gửi), giải mã gói
lệnh vừa nhận rồi trả về một gói **trạng thái phản hồi** — cùng bố cục, chỉ khác
trường Category. Nhờ vậy thử được cả hai chiều mà không cần đài thật:

  * nhãn group hiện "(serial lệnh - serial trạng thái)"
  * ô nhập / hộp chọn hiện giá trị đài đang dùng bằng chữ đỏ khi nó khác lệnh
  * nhóm nút chọn đổi màu đúng lựa chọn ứng với trạng thái
  * ba trường chỉ nhận trạng thái (AT_Azm, Beta_Back, HW_Version) có số để hiện

    python3 tools/fake_control.py
    python3 tools/fake_control.py --disobey        # trả về khác lệnh, để xem chữ đỏ
    python3 tools/fake_control.py --cmd-port 6103 --host 127.0.0.1 --status-port 6003

Cổng mặc định khớp với cấu hình mặc định của phần mềm: lệnh đi ra cổng 6103,
trạng thái về cổng 6003 (dòng "Status" trong bảng cổng nhận).

Dừng bằng Ctrl+C.

Bảng trường đầy đủ nằm ở src/net/cmdproto.h — ở đây chỉ cần đúng chừng này để
dựng lại gói trả lời.
"""

import argparse
import math
import socket
import struct
import sys
import time

# Header -> (tên, category trạng thái, số từ, các chỉ số trường chỉ-đọc)
#
# Chỉ số trường tính từ 0 và ứng với từ thứ 5+i trong gói. CMD_DSP_R và
# CMD_DSP_S dùng chung cả hai Category nên **phải** phân biệt bằng Header.
PACKETS = {
    0xA4A3A2A1: ("CMD_ANTEN",  0x70180, 10, {3: "AT_Azm"}),
    0x04030201: ("CMD_COMMON", 0x00180, 16, {}),
    0xD4D3D2D1: ("CMD_DSP_R",  0x80180, 29, {7: "Beta_Back", 12: "HW_Version"}),
    0xD9D8D7D6: ("CMD_DSP_S",  0x80180, 27, {}),
}

# Trường đem ra "không nghe lời" khi bật --disobey, cho mỗi loại gói: một nhóm
# nút chọn và một ô nhập, để thấy cả hai kiểu báo lệch.
DISOBEY = {
    "CMD_ANTEN":  {0: 0, 2: 6},        # AT_En, AT_Speed
    "CMD_COMMON": {0: 0, 4: 7},        # DataSend, Attn
    "CMD_DSP_R":  {1: 0, 11: 32768},   # DSPD_Out, ZFbeat
    "CMD_DSP_S":  {2: 0, 6: 32768},    # DSPV_Out, GainU
}

# Phiên bản HW giả: 0xyyMMddhh, hiện lên thành 2026/08/09-15.
HW_VERSION = 0x26080915


def words(data):
    n = len(data) // 4
    return list(struct.unpack(f"<{n}I", data[:n * 4]))


def build(w):
    return struct.pack(f"<{len(w)}I", *w)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cmd-port", type=int, default=6103,
                    help="cổng nghe lệnh điều khiển (mặc định 6103)")
    ap.add_argument("--host", default="127.0.0.1",
                    help="địa chỉ gửi trạng thái về (mặc định 127.0.0.1)")
    ap.add_argument("--status-port", type=int, default=6003,
                    help="cổng nhận trạng thái của phần mềm (mặc định 6003)")
    ap.add_argument("--disobey", action="store_true",
                    help="trả về giá trị khác lệnh ở vài trường, để xem phần "
                         "báo lệch bằng chữ đỏ")
    ap.add_argument("--quiet", action="store_true", help="bớt in ra màn hình")
    args = ap.parse_args()

    rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    rx.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    rx.bind(("0.0.0.0", args.cmd_port))
    tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    print(f"Nghe lệnh ở cổng {args.cmd_port}, trả trạng thái về "
          f"{args.host}:{args.status_port}"
          f"{'  (chế độ không nghe lời)' if args.disobey else ''}")
    print("Ctrl+C để dừng.\n")

    serial = {}
    started = time.monotonic()
    n = 0

    try:
        while True:
            data, src = rx.recvfrom(65535)
            if len(data) < 12:
                continue
            w = words(data)
            spec = PACKETS.get(w[0])
            if spec is None:
                if not args.quiet:
                    print(f"  bỏ qua gói lạ, header {w[0]:08x}, {len(data)} byte")
                continue

            name, status_cat, nwords, readonly = spec
            if len(w) < nwords:
                print(f"  {name}: gói cụt ({len(data)} byte, cần {nwords * 4})")
                continue

            # Trả lời: chép nguyên gói lệnh rồi thay Category và Serial.
            out = w[:nwords]
            out[1] = status_cat
            serial[name] = serial.get(name, 0) + 1
            out[3] = serial[name]

            # Phương vị "hiện tại" quay đều 6 vòng/phút, đúng dải encoder 0..4095.
            beta = int((time.monotonic() - started) / 10.0 * 4096) % 4096
            for i, field in readonly.items():
                out[5 + i] = HW_VERSION if field == "HW_Version" else beta

            if args.disobey:
                for i, v in DISOBEY.get(name, {}).items():
                    out[5 + i] = v

            tx.sendto(build(out), (args.host, args.status_port))
            n += 1
            if not args.quiet:
                body = " ".join(str(x) for x in w[5:nwords - 1])
                print(f"[{n:4d}] {name} serial={w[3]} từ {src[0]}:{src[1]}\n"
                      f"       {body}")
    except KeyboardInterrupt:
        print(f"\nĐã dừng — nhận {n} lệnh")
    finally:
        rx.close()
        tx.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
