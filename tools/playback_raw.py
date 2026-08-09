#!/usr/bin/env python3
"""Phát lại một file ghi lưu **dữ liệu gốc** (.rec) ra mạng dưới dạng gói UDP.

Dùng khi cần dựng lại một phiên đã ghi trên **máy khác** — máy không có file
ghi lưu, hay máy chỉ cài phần mềm trắc thủ. Công cụ đọc file, gửi lại từng
datagram RAW_V / RAW_P đúng cổng của nó và **đúng nhịp thời gian đã ghi**, nên
phía nhận không phân biệt được với dòng dữ liệu của đài thật.

    python3 playback_raw.py -source ./raw_20260803_103025.rec \\
                            -addr 192.168.232.238 -udpports 8200 8300

Địa chỉ quảng bá thì tự nhận ra và bật SO_BROADCAST, không phải khai thêm gì:

    python3 playback_raw.py -source ./raw_...rec -addr 192.168.232.255 \\
                            -udpports 8200 8300

Hai tham số phụ ngoài mô tả giai đoạn, để dùng cho tiện:

    -speed 4        phát nhanh gấp 4 lần (0 = nhanh nhất có thể, không chờ)
    -loop           hết file thì quay lại phát tiếp từ đầu

Dừng bằng Ctrl+C.

Định dạng file xem src/record/recordfile.h; công cụ soi file là tools/dump_rec.py.
"""

import argparse
import pathlib
import socket
import struct
import sys
import time

# --- định dạng file ghi lưu, đúng theo src/record/recordfile.h ---------------------
MAGIC = 0x6969CAFE
KIND_RAW = 0xCAFE1122
KIND_PROC = 0xCAFE3344

HEADER_BYTES = 64
REC_HEAD_BYTES = 16

REC_RAW_V = 1
REC_RAW_P = 2

TYPE_NAME = {REC_RAW_V: "RAW_V", REC_RAW_P: "RAW_P"}

# Bản ghi dài hơn mức này là file hỏng — chặn lại chứ đừng xin cấp phát vài GB.
MAX_PAYLOAD = 1 << 20


def read_header(data):
    """64 byte đầu file. None nếu không phải file ghi lưu của phần mềm."""
    if len(data) < HEADER_BYTES:
        return None
    w = struct.unpack("<16I", data[:HEADER_BYTES])
    if w[0] != MAGIC:
        return None
    return {"kind": w[1], "total": w[4], "rawV": w[5], "rawP": w[6]}


def read_records(path):
    """Đọc cả file thành danh sách (loại, mốc thời gian ms, nội dung).

    Đọc trọn vào bộ nhớ chứ không vừa đọc vừa gửi: một phiên dữ liệu gốc dài
    vài phút cũng chỉ vài trăm MB, mà đổi lại thì nhịp gửi không bao giờ bị
    một lần đọc đĩa chậm làm giật.
    """
    raw = path.read_bytes()
    head = read_header(raw)
    if head is None:
        sys.exit(f"{path}: không phải file ghi lưu (sai định danh)")
    if head["kind"] != KIND_RAW:
        kind = "dữ liệu đã qua xử lý" if head["kind"] == KIND_PROC else "lạ"
        sys.exit(f"{path}: đây là file {kind}, công cụ này chỉ phát lại "
                 f"file dữ liệu gốc (RAW_V/RAW_P)")

    items = []
    at = HEADER_BYTES
    while at + REC_HEAD_BYTES <= len(raw):
        rtype, length, tms = struct.unpack("<IIQ", raw[at:at + REC_HEAD_BYTES])
        at += REC_HEAD_BYTES
        if length > MAX_PAYLOAD or at + length > len(raw):
            print(f"!! bản ghi cụt ở byte {at}, bỏ phần còn lại của file",
                  file=sys.stderr)
            break
        # Loại lạ vẫn nhảy qua đúng số byte của nó rồi đi tiếp — đó là lý do
        # tiêu đề mỗi bản ghi mang sẵn độ dài.
        if rtype in (REC_RAW_V, REC_RAW_P):
            items.append((rtype, tms, raw[at:at + length]))
        at += length

    if not items:
        sys.exit(f"{path}: không có bản ghi RAW_V/RAW_P nào")
    return head, items


def is_broadcast(addr):
    """Địa chỉ kết thúc bằng .255 thì coi là địa chỉ quảng bá.

    Không tra được dải mạng thật từ đây (còn phụ thuộc mặt nạ của máy nhận),
    nên nhận biết theo đúng cách mô tả giai đoạn nêu: nhìn vào chính địa chỉ.
    """
    parts = addr.split(".")
    return len(parts) == 4 and parts[-1] == "255"


def play(items, sock, addr, ports, speed):
    """Gửi lại cả danh sách một lượt. Trả về số gói đã gửi của từng loại."""
    sent = {REC_RAW_V: 0, REC_RAW_P: 0}

    # Mốc so là bản ghi **đầu tiên**, không phải bản ghi liền trước: cộng dồn
    # từng khoảng một thì mỗi lần sleep trễ một chút, vài phút sau là lệch hẳn.
    first_ms = items[0][1]
    started = time.monotonic()

    for rtype, tms, payload in items:
        if speed > 0:
            wait = (tms - first_ms) / 1000.0 / speed - (time.monotonic() - started)
            if wait > 0:
                time.sleep(wait)
        sock.sendto(payload, (addr, ports[rtype]))
        sent[rtype] += 1

    return sent


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-source", required=True, metavar="FILE",
                    help="đường dẫn đến file ghi lưu dữ liệu gốc (.rec)")
    ap.add_argument("-addr", required=True, metavar="IP",
                    help="địa chỉ IP máy nhận; kết thúc bằng .255 là gửi quảng bá")
    ap.add_argument("-udpports", required=True, nargs=2, type=int,
                    metavar=("RAW_V", "RAW_P"),
                    help="cổng nhận RAW_V và cổng nhận RAW_P của máy nhận")
    ap.add_argument("-speed", type=float, default=1.0, metavar="X",
                    help="hệ số tốc độ phát (mặc định 1 = đúng nhịp đã ghi, "
                         "0 = nhanh nhất có thể)")
    ap.add_argument("-loop", action="store_true",
                    help="hết file thì quay lại phát từ đầu")
    args = ap.parse_args()

    for p in args.udpports:
        if not 1 <= p <= 65535:
            sys.exit(f"cổng {p} không hợp lệ")
    if args.speed < 0:
        sys.exit("hệ số tốc độ không được âm")

    head, items = read_records(pathlib.Path(args.source))
    ports = {REC_RAW_V: args.udpports[0], REC_RAW_P: args.udpports[1]}
    span = (items[-1][1] - items[0][1]) / 1000.0

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    bcast = is_broadcast(args.addr)
    if bcast:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    print(f"Nguồn : {args.source}")
    print(f"Đích  : {args.addr}{'  (quảng bá)' if bcast else ''}   "
          f"RAW_V -> {ports[REC_RAW_V]}, RAW_P -> {ports[REC_RAW_P]}")
    print(f"Nội dung: {len(items):,} gói ({head['rawV']:,} RAW_V, "
          f"{head['rawP']:,} RAW_P theo header), dài {span:.1f} giây")
    print(f"Tốc độ: {'nhanh nhất có thể' if args.speed == 0 else f'{args.speed}x'}"
          f"{'  (lặp lại)' if args.loop else ''}")
    print("Ctrl+C để dừng.\n")

    total = {REC_RAW_V: 0, REC_RAW_P: 0}
    rounds = 0
    try:
        while True:
            sent = play(items, sock, args.addr, ports, args.speed)
            for k in total:
                total[k] += sent[k]
            rounds += 1
            print(f"[lượt {rounds}] đã gửi " + ", ".join(
                f"{TYPE_NAME[k]} {v:,}" for k, v in total.items()))
            if not args.loop:
                break
    except KeyboardInterrupt:
        print("\nĐã dừng — đã gửi " + ", ".join(
            f"{TYPE_NAME[k]} {v:,}" for k, v in total.items()))
    finally:
        sock.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
