#!/usr/bin/env python3
"""Tạo giả gói tin RAW_V và RAW_P đồng bộ, có ba mục tiêu chuyển động.

Khác fake_raw_v.py ở chỗ công cụ này phát cả hai loại gói **từ cùng một nguồn
phương vị**: mỗi chu kỳ gửi một RAW_V và một RAW_P mang đúng một giá trị
Azimuth. Thuật toán tâm chùm và bộ bám quỹ đạo chỉ chạy đúng khi hai luồng đó
khớp nhau, nên đây là điều kiện bắt buộc chứ không phải chi tiết làm cho đẹp.

Ba mục tiêu theo mô tả giai đoạn 4:

  1. Bay vòng tròn xuôi kim đồng hồ, ~2.0 m/s, ô cự ly 350, bắt đầu ở 45°.
  2. Bay thẳng từ tâm đài ra, hướng 125°, ~5.5 m/s, từ ô 10 tới ô 700 rồi
     quay lại từ đầu.
  3. Bay xuyên tâm đài từ phương vị 315° ô 150, ~15.5 m/s, sang tới ô 200 ở
     phía bên kia thì quay đầu 180°.

    python3 tools/fake_radar.py
    python3 tools/fake_radar.py --host 127.0.0.1 --port-v 6001 --port-p 6002

Dừng bằng Ctrl+C.
"""

import argparse
import math
import random
import socket
import struct
import sys
import time

HEADER_V = 0xB4B3B2B1
CATEGORY_V = 0x20180
BINS = 1024
WORDS_V = 6 + BINS + 1          # 1031 từ, khớp trường Length
LENGTH_V = WORDS_V * 4

HEADER_P = 0xC4C3C2C1
CATEGORY_P = 0x30180
PLOTS = 64                      # Data_P[64]
MAX_PLOTS = PLOTS - 1           # Data_P[0] là từ tiêu đề chu kỳ
WORDS_P = 7 + PLOTS + 1         # 72 từ
LENGTH_P = WORDS_P * 4

AZIMUTH_STEPS = 4096
RANGE_CELLS = 1024

# Cự ly tối đa mặc định (mét), ứng với Fs=1, B=154, Tc=2500.
RMAX_M = 1218.0

# Nền tạp trên thang 256; ZFbeat mặc định 32768 nên giá trị thô = mức * 128.
NOISE_MIN, NOISE_MAX = 5, 8
TARGET_MIN, TARGET_MAX = 150, 180


def clamp(v, lo, hi):
    return lo if v < lo else (hi if v > hi else v)


def wrap_delta_deg(a):
    """Chênh lệch góc theo đường ngắn nhất, trong (-180, 180]."""
    return (a + 180.0) % 360.0 - 180.0


class Target:
    """Một mục tiêu giả.

    Vị trí giữ ở dạng cực (phương vị độ, ô cự ly) vì cả hai loại gói đều phát
    biểu theo hệ đó. Quy luật chuyển động thì tính trong hệ Đề-các cho mục tiêu
    bay thẳng — bay thẳng qua gần tâm đài mà tính trong hệ cực thì phương vị
    nhảy vọt, rất khó viết cho đúng.
    """

    def __init__(self, name, azm_deg, cell, num_min, num_max,
                 amp_min, amp_max, dopler):
        self.name = name
        self.azm = azm_deg
        self.cell = float(cell)
        self.num_min = num_min
        self.num_max = num_max
        self.amp_min = amp_min
        self.amp_max = amp_max
        self.dopler = dopler

        # Số xung của lần chùm tia quét qua hiện tại. Chọn lại mỗi vòng để số
        # xung trong chùm dao động đúng dải đã cho.
        self.numcx = random.randint(num_min, num_max)

        # Độ lệch ô cự ly và dopler của lần quét qua này.
        #
        # Chọn một lần cho cả chùm chứ không random từng xung: một lần chùm tia
        # quét qua chỉ kéo dài 40-160 ms, trong ngần ấy thời gian ô cự ly và
        # dopler của mục tiêu thực tế không nhảy loạn. Quan trọng hơn, thuật
        # toán xét duyệt plot vào chùm so với **xung đầu chùm** với tiêu chuẩn
        # ±1: random từng xung thì xung đầu rơi vào -1 là mọi xung +1 bị loại,
        # chùm bị cắt đôi và một mục tiêu ra hai điểm dấu.
        self.cell_offset = 0
        self.dopler_offset = 0

    def new_pass(self):
        self.numcx = random.randint(self.num_min, self.num_max)
        self.cell_offset = random.choice((-1, 0, 1))
        self.dopler_offset = random.choice((-1, 0, 1))

    def step(self, dt):
        raise NotImplementedError

    def amplitude(self):
        return random.randint(self.amp_min, self.amp_max)

    def dopler_value(self):
        return clamp(self.dopler + self.dopler_offset, 0, 31)

    def cell_value(self):
        return clamp(int(round(self.cell)) + self.cell_offset, 0, RANGE_CELLS - 1)


class CircleTarget(Target):
    """Bay vòng tròn xuôi kim đồng hồ, giữ nguyên ô cự ly."""

    def step(self, dt):
        radius_m = max(1.0, self.cell * RMAX_M / RANGE_CELLS)
        # Vận tốc góc suy từ vận tốc dài: chậm dần khi ở xa, đúng như thật.
        self.azm = (self.azm + math.degrees(self.speed / radius_m) * dt) % 360.0


class RadialTarget(Target):
    """Bay thẳng theo phương xuyên tâm, ra xa rồi lặp lại từ đầu."""

    def step(self, dt):
        self.cell += self.speed * dt * RANGE_CELLS / RMAX_M
        if self.cell > self.cell_stop:
            self.cell = self.cell_start


class CrossTarget(Target):
    """Bay xuyên qua tâm đài rồi quay đầu 180 độ.

    Giữ vị trí ở dạng một toạ độ chạy dọc theo đường bay: dương là còn ở phía
    xuất phát, âm là đã qua bên kia tâm đài. Nhờ vậy lúc đi qua đúng tâm đài
    không phải xử lý trường hợp riêng nào cả.
    """

    def step(self, dt):
        step_cells = self.speed * dt * RANGE_CELLS / RMAX_M
        self.pos -= step_cells * self.direction

        if self.direction > 0 and self.pos < -self.cell_far:
            self.direction = -1
        elif self.direction < 0 and self.pos > self.cell_start:
            self.direction = 1

        # Toạ độ âm nghĩa là đã sang bên kia tâm đài: phương vị đảo 180 độ, ô
        # cự ly lấy trị tuyệt đối.
        if self.pos >= 0.0:
            self.azm = self.azm_home
            self.cell = self.pos
        else:
            self.azm = (self.azm_home + 180.0) % 360.0
            self.cell = -self.pos


def build_targets():
    t1 = CircleTarget("MT1 vòng tròn", 45.0, 350, 16, 24, 12000, 17000, 4)
    t1.speed = 2.0

    t2 = RadialTarget("MT2 xuyên tâm ra", 125.0, 10, 32, 48, 20000, 30000, 8)
    t2.speed = 5.5
    t2.cell_start = 10.0
    t2.cell_stop = 700.0

    t3 = CrossTarget("MT3 qua tâm đài", 315.0, 150, 48, 64, 50000, 80000, 15)
    t3.speed = 15.5
    t3.azm_home = 315.0
    t3.cell_start = 150.0
    t3.cell_far = 200.0      # đi tiếp 200 ô sang bên kia tâm đài
    t3.pos = 150.0
    t3.direction = 1

    return [t1, t2, t3]


def build_raw_v(serial, azimuth, data):
    head = struct.pack("<6I", HEADER_V, CATEGORY_V, LENGTH_V, serial, 0, azimuth)
    body = struct.pack("<{}I".format(BINS), *data)
    return head + body + struct.pack("<I", 0)


def build_raw_p(serial, azimuth, cycle_count, plots):
    """plots: danh sách (cell, amplitude, dopler)."""
    words = [0] * PLOTS
    # Data_P[0]: bit 0..11 phương vị, bit 12..30 số đếm, bit 31 dấu đầu chu kỳ.
    words[0] = (azimuth & 0x0FFF) | ((cycle_count & 0x7FFFF) << 12) | (1 << 31)
    for i, (cell, amp, dop) in enumerate(plots[:MAX_PLOTS]):
        words[i + 1] = (amp & 0xFFFF) | ((cell & 0x03FF) << 16) | ((dop & 0x1F) << 26)

    head = struct.pack("<7I", HEADER_P, CATEGORY_P, LENGTH_P, serial, 0,
                       azimuth, len(plots[:MAX_PLOTS]))
    return head + struct.pack("<{}I".format(PLOTS), *words) + struct.pack("<I", 0)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="127.0.0.1", help="địa chỉ đích")
    ap.add_argument("--port-v", type=int, default=6001, help="cổng RAW_V")
    ap.add_argument("--port-p", type=int, default=6002, help="cổng RAW_P")
    ap.add_argument("--period", type=float, default=2.5,
                    help="chu kỳ gửi, mili giây (mặc định 2.5)")
    ap.add_argument("--rpm", type=float, default=6.0,
                    help="tốc độ vòng quét, vòng/phút (mặc định 6)")
    ap.add_argument("--zfbeat", type=int, default=32768,
                    help="hệ số căn chỉnh biên độ, phải khớp tab Tham số")
    ap.add_argument("--no-video", action="store_true",
                    help="chỉ gửi RAW_P (để nhìn riêng lớp điểm dấu)")
    ap.add_argument("--dropout", type=float, default=0.04,
                    help="tỉ lệ xung bị mất giữa chùm (mặc định 0.04) — để "
                         "tiêu chuẩn CX_NUM_LOSE có việc mà làm")
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dest_v = (args.host, args.port_v)
    dest_p = (args.host, args.port_p)

    period = args.period / 1000.0
    step_deg = 360.0 * (args.rpm / 60.0) * period      # phương vị nhích mỗi chu kỳ
    scale = max(1, args.zfbeat // 256)                 # mức 0..255 -> giá trị thô

    targets = build_targets()

    print("Gửi RAW_V tới {}:{} và RAW_P tới {}:{}"
          .format(args.host, args.port_v, args.host, args.port_p))
    print("  {:.0f} gói/giây mỗi loại, {:.1f} vòng/phút, {} mục tiêu"
          .format(1.0 / period, args.rpm, len(targets)))

    serial = 1
    azimuth_deg = 0.0
    cycle_count = 0
    # Nhớ lần trước từng mục tiêu có nằm trong chùm tia không, để chọn lại số
    # xung mỗi khi chùm tia bắt đầu quét qua nó.
    was_lit = [False] * len(targets)

    next_send = time.perf_counter()
    reported = next_send

    try:
        while True:
            az_enc = int(azimuth_deg * AZIMUTH_STEPS / 360.0) % AZIMUTH_STEPS

            video = [random.randint(NOISE_MIN, NOISE_MAX) * scale
                     for _ in range(BINS)]
            plots = []

            for i, t in enumerate(targets):
                # Bề rộng chùm tia suy ngược từ số xung mong muốn: mỗi chu kỳ
                # phương vị nhích step_deg, muốn N xung thì chùm rộng N nấc.
                half = t.numcx * step_deg / 2.0
                lit = abs(wrap_delta_deg(azimuth_deg - t.azm)) <= half

                if lit and not was_lit[i]:
                    pass                      # vừa vào chùm, giữ nguyên numcx
                elif not lit and was_lit[i]:
                    t.new_pass()              # vừa ra khỏi chùm, chọn số xung mới
                was_lit[i] = lit

                if not lit:
                    continue

                cell = t.cell_value()

                # Nền tạp vọt lên ở khu vực ô cự ly của mục tiêu ngay cả khi
                # xung bị mất: nền tạp là tín hiệu thô, mất plot là chuyện của
                # tầng phát hiện phía sau.
                for k in range(max(0, cell - 2), min(BINS, cell + 3)):
                    video[k] = random.randint(TARGET_MIN, TARGET_MAX) * scale

                if random.random() < args.dropout:
                    continue

                plots.append((cell, t.amplitude(), t.dopler_value()))

            if not args.no_video:
                sock.sendto(build_raw_v(serial, az_enc, video), dest_v)
            sock.sendto(build_raw_p(serial, az_enc, cycle_count, plots), dest_p)

            for t in targets:
                t.step(period)

            serial = serial + 1 if serial < 0xFFFFFFFF else 1
            cycle_count = (cycle_count + 1) & 0x7FFFF
            azimuth_deg = (azimuth_deg + step_deg) % 360.0

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
                print("  {} gói, phương vị {:.1f}° — ".format(serial - 1, azimuth_deg)
                      + ", ".join("{}: {:.1f}°/{:.0f}".format(t.name, t.azm, t.cell)
                                  for t in targets))
    except KeyboardInterrupt:
        print("\nĐã dừng sau {} gói mỗi loại.".format(serial - 1))

    return 0


if __name__ == "__main__":
    sys.exit(main())
