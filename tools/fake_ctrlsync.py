#!/usr/bin/env python3
"""Đóng vai **một máy tính khác** trong hệ thống, để thử phần đồng bộ điều khiển.

Trong hệ thống thật có nhiều màn hình trắc thủ, nhưng tại một thời điểm chỉ một
máy được ra lệnh cho đài. Máy nào bấm "Mở khóa điều khiển" thì quảng bá một gói
CTRL_SYNC, và mọi máy khác nghe được sẽ tự khóa lại. Công cụ này làm đúng hai
việc của "máy khác" đó:

  * gửi đi một (hay nhiều) gói CTRL_SYNC — phần mềm đang mở khóa sẽ tự khóa lại,
    nhãn "CtrlIP: ..." trong tab "Điều khiển" đổi sang địa chỉ đưa vào đây
  * nghe và in ra các gói CTRL_SYNC của phần mềm, để thấy chiều ngược lại

    # gửi một gói, đóng vai máy 192.168.11.30
    python3 tools/fake_ctrlsync.py --ctrl-ip 192.168.11.30

    # chỉ nghe xem phần mềm quảng bá những gì
    python3 tools/fake_ctrlsync.py --listen

    # gửi lại mỗi 5 giây, để xem phần mềm có bị khóa lại sau mỗi lần mở khóa
    python3 tools/fake_ctrlsync.py --ctrl-ip 10.0.0.9 --repeat 5

Mặc định gửi tới 127.0.0.1:9113 — đúng cấu hình chạy thử trên một máy. Thử trên
mạng thật thì đưa --host là địa chỉ quảng bá của dải đó, ví dụ
--host 192.168.11.255.

**CtrlIP phải khác LocalIP của dòng "CtrlSync_R"** trong bảng cổng nhận của phần
mềm: trùng thì phần mềm hiểu đó là gói do chính nó quảng bá và bỏ qua — đúng như
mô tả giao thức.

Bố cục gói nằm ở src/net/syncproto.h.
"""

import argparse
import ipaddress
import socket
import struct
import sys
import time

HEADER = 0xCAFE9113
CATEGORY = 0x9113
WORDS = 7
SIZE = WORDS * 4
PORT = 9113


def build(ctrl_ip, serial, time_ms=0):
    """Gói CTRL_SYNC: Header, Category, Length, Serial, Time, CtrlIP, CheckSum."""
    # CtrlIP là địa chỉ dạng số với byte đầu của địa chỉ ở byte cao
    # (192.168.11.22 -> 0xC0A80B16), đúng như syncproto::build() bên C++.
    ip = int(ipaddress.IPv4Address(ctrl_ip))
    return struct.pack("<7I", HEADER, CATEGORY, SIZE, serial, time_ms, ip, 0)


def parse(data):
    """(serial, time, CtrlIP) của một gói CTRL_SYNC, hoặc None nếu không phải."""
    if len(data) < SIZE:
        return None
    w = struct.unpack("<7I", data[:SIZE])
    if w[0] != HEADER or w[1] != CATEGORY:
        return None
    return w[3], w[4], str(ipaddress.IPv4Address(w[5]))


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ctrl-ip", default="192.168.11.30",
                    help="địa chỉ đi trong trường CtrlIP — đóng vai máy nào "
                         "(mặc định 192.168.11.30)")
    ap.add_argument("--host", default="127.0.0.1",
                    help="địa chỉ gửi tới; dùng địa chỉ quảng bá của dải khi thử "
                         "trên mạng thật (mặc định 127.0.0.1)")
    ap.add_argument("--port", type=int, default=PORT,
                    help=f"cổng gửi tới, cũng là cổng nghe (mặc định {PORT})")
    ap.add_argument("--repeat", type=float, default=0.0,
                    help="gửi lại sau mỗi N giây; 0 = gửi đúng một gói")
    ap.add_argument("--listen", action="store_true",
                    help="chỉ nghe và in ra các gói CTRL_SYNC, không gửi gì")
    args = ap.parse_args()

    if args.listen:
        rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        rx.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        rx.bind(("0.0.0.0", args.port))
        print(f"Nghe CTRL_SYNC ở cổng {args.port}. Ctrl+C để dừng.\n")
        n = 0
        try:
            while True:
                data, src = rx.recvfrom(65535)
                got = parse(data)
                if got is None:
                    print(f"  bỏ qua gói lạ từ {src[0]}:{src[1]}, "
                          f"{len(data)} byte")
                    continue
                n += 1
                serial, time_ms, ctrl_ip = got
                print(f"[{n:4d}] CTRL_SYNC serial={serial} time={time_ms} "
                      f"CtrlIP={ctrl_ip}  (từ {src[0]}:{src[1]})")
        except KeyboardInterrupt:
            print(f"\nĐã dừng — nhận {n} gói")
        finally:
            rx.close()
        return 0

    tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    tx.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    print(f"Gửi CTRL_SYNC tới {args.host}:{args.port}, CtrlIP={args.ctrl_ip}")
    serial = 0
    try:
        while True:
            serial += 1
            tx.sendto(build(args.ctrl_ip, serial), (args.host, args.port))
            print(f"[{serial:4d}] đã gửi — phần mềm đang mở khóa sẽ tự khóa lại")
            if args.repeat <= 0:
                break
            time.sleep(args.repeat)
    except KeyboardInterrupt:
        print(f"\nĐã dừng — gửi {serial} gói")
    finally:
        tx.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
