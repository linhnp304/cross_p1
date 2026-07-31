#!/usr/bin/env python3
"""Tạo giả gói tin RAW_V để kiểm tra phần nhận và hiển thị nền tạp.

Gửi UDP tới cổng UDP-RAW_V của phần mềm (mặc định 127.0.0.1:6001), nhịp 2.5 ms
(~400 gói/giây), phương vị chạy đủ 6 vòng/phút.

    python3 tools/fake_raw_v.py
    python3 tools/fake_raw_v.py --port 6001 --rpm 6 --targets 3

Dừng bằng Ctrl+C.
"""

import argparse
import random
import socket
import struct
import sys
import time

HEADER = 0xB4B3B2B1
CATEGORY = 0x20180
BINS = 1024
WORDS = 6 + BINS + 1          # 1031 từ, khớp trường Length
LENGTH = WORDS * 4
AZIMUTH_STEPS = 4096

# Dải nền tạp theo mô tả giai đoạn 3.
NOISE_MIN = 10000
NOISE_MAX = 15000

def build_packet(serial: int, azimuth: int, data: list) -> bytes:
    """Gói xếp liền, little-endian: header, category, length, serial, time,
    azimuth, 1024 điểm biên độ, checksum (chưa dùng, luôn 0)."""
    head = struct.pack("<6I", HEADER, CATEGORY, LENGTH, serial, 0, azimuth)
    body = struct.pack("<{}I".format(BINS), *data)
    return head + body + struct.pack("<I", 0)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="127.0.0.1", help="địa chỉ đích")
    ap.add_argument("--port", type=int, default=6001, help="cổng đích")
    ap.add_argument("--period", type=float, default=2.5,
                    help="chu kỳ gửi, mili giây (mặc định 2.5)")
    ap.add_argument("--rpm", type=float, default=6.0,
                    help="tốc độ vòng quét, vòng/phút (mặc định 6)")
    ap.add_argument("--targets", type=int, default=0,
                    help="số mục tiêu giả để dễ nhìn ra thang cự ly (mặc định 0, "
                         "tức đúng như mô tả: chỉ có nền tạp)")
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dest = (args.host, args.port)

    period = args.period / 1000.0
    # Mỗi gói phương vị nhích lên bao nhiêu nấc để đúng tốc độ vòng quét.
    step = AZIMUTH_STEPS * (args.rpm / 60.0) * period

    # Mục tiêu giả: mỗi cái là một vệt sáng ở một ô cự ly và một phương vị.
    targets = [(random.randrange(BINS), random.randrange(AZIMUTH_STEPS))
               for _ in range(args.targets)]

    print("Gửi RAW_V tới {}:{} — {:.1f} gói/giây, {:.1f} vòng/phút, {} mục tiêu"
          .format(args.host, args.port, 1.0 / period, args.rpm, len(targets)))

    serial = 1
    azimuth = 0.0
    next_send = time.perf_counter()
    reported = next_send

    try:
        while True:
            az = int(azimuth) % AZIMUTH_STEPS
            data = [random.randint(NOISE_MIN, NOISE_MAX) for _ in range(BINS)]

            for rng, taz in targets:
                # Chùm tia rộng vài độ nên mục tiêu hiện ra trên nhiều gói liền.
                delta = (az - taz + AZIMUTH_STEPS // 2) % AZIMUTH_STEPS \
                        - AZIMUTH_STEPS // 2
                if abs(delta) < 40:
                    for i in range(max(0, rng - 2), min(BINS, rng + 3)):
                        data[i] = random.randint(60000, 65535)

            sock.sendto(build_packet(serial, az, data), dest)

            serial = serial + 1 if serial < 0xFFFFFFFF else 1
            azimuth += step

            # Nhịp theo đồng hồ tuyệt đối: cộng dồn sleep sẽ trôi dần và tốc độ
            # vòng quét không còn đúng nữa.
            next_send += period
            now = time.perf_counter()
            if next_send > now:
                time.sleep(next_send - now)
            else:
                next_send = now   # máy không theo kịp, bỏ phần đã trễ

            if now - reported >= 5.0:
                reported = now
                print("  đã gửi {} gói, phương vị {} ({:.1f}°)"
                      .format(serial - 1, az, az * 360.0 / AZIMUTH_STEPS))
    except KeyboardInterrupt:
        print("\nĐã dừng sau {} gói.".format(serial - 1))

    return 0


if __name__ == "__main__":
    sys.exit(main())
