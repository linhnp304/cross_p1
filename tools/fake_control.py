#!/usr/bin/env python3
"""Đóng vai đài để thử tab "Điều khiển" và cửa sổ "Điều khiển ADF4159".

Công cụ nghe trên cổng lệnh (dòng "Command" trong bảng cổng gửi), giải mã gói
lệnh vừa nhận rồi trả về một gói **trạng thái phản hồi** — cùng bố cục, chỉ khác
trường Category. Nhờ vậy thử được cả hai chiều mà không cần đài thật:

  * nhãn group hiện "(serial lệnh - serial trạng thái)"
  * ô nhập / hộp chọn hiện giá trị đài đang dùng bằng chữ đỏ khi nó khác lệnh
  * nhóm nút chọn đổi màu đúng lựa chọn ứng với trạng thái
  * ba trường chỉ nhận trạng thái (AT_Azm, Beta_Back, HW_Version) có số để hiện
  * dòng giá trị trả về dưới mỗi ô thanh ghi của cửa sổ ADF4159

    python3 tools/fake_control.py
    python3 tools/fake_control.py --disobey        # trả về khác lệnh, để xem chữ đỏ
    python3 tools/fake_control.py --cmd-port 6103 --host 127.0.0.1 --status-port 6003

Lệnh đi ra cổng 6103, đúng cấu hình mặc định của phần mềm. Trạng thái thì mặc
định trả **về đúng địa chỉ và cổng nguồn của gói lệnh vừa nhận**, giống hệt hệ
thống thật — mà cổng nguồn ấy do hệ điều hành của máy chạy phần mềm tự chọn nên
mỗi lần một số khác. Đưa --status-port (và --host) thì quay lại kiểu cũ: trả về
một cổng cố định, tức dòng "Status" trong bảng cổng nhận. Kit ADF4159 dùng chung
đường ấy — nó nằm trong cùng một đài, không có đường riêng.

Dừng bằng Ctrl+C.

Bảng trường đầy đủ nằm ở src/net/cmdproto.h và src/net/adfproto.h — ở đây chỉ
cần đúng chừng này để dựng lại gói trả lời.
"""

import argparse
import math
import socket
import struct
import sys
import time

# Header -> (tên, category trạng thái, số từ, các chỉ số trường chỉ-đọc, in hex)
#
# Chỉ số trường tính từ 0 và ứng với từ thứ 5+i trong gói. Tra theo Header vì
# nó là khoá duy nhất của cả bốn gói; Category từ 11/08/2026 cũng đã khác nhau
# hết, nhưng trước đó CMD_DSP_R và CMD_DSP_S từng dùng chung.
#
# Hai gói ADF4159 mang thanh ghi thô nên in ra dạng hex mới đối chiếu được với
# ô hex trên cửa sổ; các gói còn lại là số đo nên in thập phân.
PACKETS = {
    0xA4A3A2A1: ("CMD_ANTEN",  0x70180, 10, {3: "AT_Azm"}, False),
    0x04030201: ("CMD_COMMON", 0x00180, 16, {}, False),
    0xD4D3D2D1: ("CMD_DSP_R",  0x80180, 29, {7: "Beta_Back", 12: "HW_Version"}, False),
    0xE4E3E2E1: ("CMD_DSP_S",  0x90180, 27, {}, False),
    0xADF4159A: ("CMD_ADF4159_REG8", 0x50180, 14, {}, True),
    0xADF4159B: ("CMD_ADF4159_REG",  0x60180,  7, {}, True),
}

# Trường đem ra "không nghe lời" khi bật --disobey, cho mỗi loại gói: một nhóm
# nút chọn và một ô nhập, để thấy cả hai kiểu báo lệch.
DISOBEY = {
    "CMD_ANTEN":  {0: 0, 2: 6},        # AT_En, AT_Speed
    "CMD_COMMON": {0: 0, 4: 7},        # DataSend, Attn
    "CMD_DSP_R":  {1: 0, 11: 32768},   # DSPD_Out, ZFbeat
    "CMD_DSP_S":  {2: 0, 6: 32768},    # DSPV_Out, GainU
}

# Thanh ghi đem ra "không nghe lời", cho hai gói ADF4159. Ở đây **lật một bit**
# chứ không gán số khác: ba bit thấp nhất của một từ thanh ghi là số hiệu thanh
# ghi, mà bit 6 (R4) và bit 23 (R5/R6) là bit chọn nhánh quét — gán bừa một số
# là gói trả về rơi vào nhầm ô trên cửa sổ. Bit 30 không mang nhiệm vụ nào
# trong cả ba việc đó.
DISOBEY_ADF = {
    "CMD_ADF4159_REG8": [2, 5],   # R2 và R5
    "CMD_ADF4159_REG":  [0],      # thanh ghi duy nhất của gói
}
DISOBEY_ADF_BIT = 1 << 30

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
    ap.add_argument("--host", default="",
                    help="địa chỉ gửi trạng thái về (mặc định: đúng địa chỉ "
                         "nguồn của gói lệnh)")
    ap.add_argument("--status-port", type=int, default=0,
                    help="cổng nhận trạng thái của phần mềm; mặc định 0 = trả "
                         "về đúng cổng nguồn của gói lệnh, như đài thật")
    ap.add_argument("--disobey", action="store_true",
                    help="trả về giá trị khác lệnh ở vài trường, để xem phần "
                         "báo lệch bằng chữ đỏ")
    ap.add_argument("--quiet", action="store_true", help="bớt in ra màn hình")
    args = ap.parse_args()

    rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    rx.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    rx.bind(("0.0.0.0", args.cmd_port))
    tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    where = (f"{args.host or 'nguồn'}:{args.status_port}" if args.status_port
             else "đúng nơi gói lệnh đi ra")
    print(f"Nghe lệnh ở cổng {args.cmd_port}, trả trạng thái về {where}"
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

            name, status_cat, nwords, readonly, as_hex = spec
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
                for i in DISOBEY_ADF.get(name, []):
                    out[5 + i] ^= DISOBEY_ADF_BIT

            # Đài thật trả lời về đúng nơi câu hỏi đi ra, nên mặc định ở đây
            # cũng vậy. --status-port là để thử lại kiểu cổng cố định.
            dest = (args.host or src[0],
                    args.status_port) if args.status_port else src
            tx.sendto(build(out), dest)
            n += 1
            if not args.quiet:
                fmt = (lambda x: f"0x{x:08X}") if as_hex else str
                body = " ".join(fmt(x) for x in w[5:nwords - 1])
                print(f"[{n:4d}] {name} serial={w[3]} từ {src[0]}:{src[1]}"
                      f" -> {dest[0]}:{dest[1]}\n"
                      f"       {body}")
    except KeyboardInterrupt:
        print(f"\nĐã dừng — nhận {n} lệnh")
    finally:
        rx.close()
        tx.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
