#!/usr/bin/env python3
"""Tạo giả gói tin PlotTC ba mục tiêu, để kiểm tra riêng bộ lọc Kalman.

Khác fake_radar.py ở chỗ công cụ này **không** phát RAW_P: điểm dấu được tính
sẵn ở đây rồi gửi thẳng dưới dạng gói PlotTC. Nhờ vậy phần đang thử là đúng bộ
bám quỹ đạo, không lẫn với thuật toán tách chùm xung — chùm xung sai một chút
là điểm dấu lệch đi, mà lệch thì không biết lỗi ở bộ lọc hay ở khâu trước nó.

RAW_V vẫn được phát, chỉ để làm nền tạp và để **đồng bộ đường quét**: phần mềm
chốt sổ mỗi vòng quét theo phương vị, không có dòng RAW_V thì bộ bám không biết
lúc nào hết một vòng. Nền tạp phát theo đúng giao thức trong tài liệu (Data_V là
1024 từ 4 byte bắt đầu từ word 6), giống fake_radar.py.

Ba mục tiêu theo mô tả giai đoạn 6:

  1. Bay vòng tròn xuôi kim đồng hồ, bắt đầu phương vị 90°, cự ly 250 m, 2 m/s.
     Chu kỳ có/mất tiêu: 5 vòng có, 1 mất, 2 có, 2 mất, 3 có, 5 mất.
  2. Bay thẳng từ tâm đài ra, phương vị 135°, cự ly đầu 75 m, 1.5 m/s.
     Chu kỳ: 1 vòng có, 1 vòng mất.
  3. Bay thẳng vào tâm đài, phương vị 295°, từ 1100 m tới 75 m rồi lặp lại,
     1.5 m/s. Chu kỳ: 1 có, 2 mất, 5 có, 4 mất.

Cổng nhận mặc định của loại dữ liệu "Plot" trong tab "Kết nối" là 6004 — dòng
đó **không** được tạo sẵn, phải tự bấm "Thêm dòng" một lần.

    python3 tools/fake_plottc.py
    python3 tools/fake_plottc.py --host 127.0.0.1 --port-v 6001 --port-plot 6004

Dừng bằng Ctrl+C.
"""

import argparse
import math
import random
import socket
import struct
import time

# --- RAW_V, đúng như mô tả giao thức giai đoạn 3 ---------------------------
HEADER_V = 0xB4B3B2B1
CATEGORY_V = 0x20180
BINS = 1024
WORDS_V = 6 + BINS + 1
LENGTH_V = WORDS_V * 4

# --- PlotTC, đúng như packetio.h -------------------------------------------
HEADER_TC = 0x2D2D2D2D
CATEGORY_PLOT = 0x2021
WORDS_PLOT = 26
LENGTH_PLOT = WORDS_PLOT * 4

AZIMUTH_STEPS = 4096

# Cự ly tối đa mặc định (mét), ứng với Fs=1, B=154, Tc=2500.
RMAX_M = 1218.0

# Nền tạp trên thang 256; ZFbeat mặc định 32768 nên giá trị thô = mức * 128.
NOISE_MIN, NOISE_MAX = 5, 8


def wrap_delta_deg(a):
    """Chênh lệch góc theo đường ngắn nhất, trong (-180, 180]."""
    return (a + 180.0) % 360.0 - 180.0


class Target:
    """Một mục tiêu giả.

    `pattern` là chu kỳ có/mất tiêu tính theo **vòng quét**, dạng danh sách các
    cặp (có tiêu?, số vòng). Đếm theo vòng chứ không theo giây: tiêu chuẩn khởi
    tạo và số vòng ngoại suy của bộ bám đều phát biểu theo vòng quét.
    """

    def __init__(self, name, azm_deg, range_m, speed_ms, pattern):
        self.name = name
        self.azm = float(azm_deg)
        self.range_m = float(range_m)
        self.speed = float(speed_ms)
        self.pattern = pattern

        # Trải chu kỳ ra thành một dãy True/False cho dễ tra.
        self.blips = []
        for present, count in pattern:
            self.blips.extend([present] * count)

        self.scan = 0          # số vòng quét đã đi qua mục tiêu này
        self.prev_delta = None # vị trí chùm tia so với mục tiêu ở nhịp trước

    def visible(self):
        return self.blips[self.scan % len(self.blips)]

    def advance(self, dt):
        raise NotImplementedError


class CircleTarget(Target):
    """Bay vòng tròn quanh tâm đài, xuôi chiều kim đồng hồ."""

    def advance(self, dt):
        # Vận tốc dài đổi ra vận tốc góc: omega = v / r.
        self.azm = (self.azm + math.degrees(self.speed / self.range_m) * dt) % 360.0


class RadialTarget(Target):
    """Bay thẳng dọc theo một phương vị, ra xa hoặc vào gần tâm đài."""

    def __init__(self, name, azm_deg, range_m, speed_ms, pattern,
                 outward, near_m, far_m):
        super().__init__(name, azm_deg, range_m, speed_ms, pattern)
        self.outward = outward
        self.near_m = near_m
        self.far_m = far_m

    def advance(self, dt):
        self.range_m += (self.speed if self.outward else -self.speed) * dt
        # Ra khỏi dải thì quay về đầu — để chạy công cụ một lúc là xem được cả
        # vòng đời quỹ đạo: khởi tạo, bám, mất, xoá, rồi khởi tạo lại.
        if self.outward and self.range_m > self.far_m:
            self.range_m = self.near_m
        elif not self.outward and self.range_m < self.near_m:
            self.range_m = self.far_m


def build_targets():
    return [
        CircleTarget("1 — vòng tròn", 90.0, 250.0, 2.0,
                     [(True, 5), (False, 1), (True, 2),
                      (False, 2), (True, 3), (False, 5)]),
        RadialTarget("2 — ra xa", 135.0, 75.0, 1.5,
                     [(True, 1), (False, 1)],
                     outward=True, near_m=75.0, far_m=RMAX_M),
        RadialTarget("3 — vào gần", 295.0, 1100.0, 1.5,
                     [(True, 1), (False, 2), (True, 5), (False, 4)],
                     outward=False, near_m=75.0, far_m=1100.0),
    ]


def build_raw_v(serial, azimuth, video):
    head = struct.pack("<6I", HEADER_V, CATEGORY_V, LENGTH_V, serial, 0, azimuth)
    return head + struct.pack("<{}I".format(BINS), *video) + struct.pack("<I", 0)


def build_plot(serial, time_ms, azm_deg, range_m, amplitude):
    """Gói PlotTC 26 từ. Thứ tự trường đọc song song với packetio.h::buildPlot."""
    words = [
        HEADER_TC,
        CATEGORY_PLOT,
        LENGTH_PLOT,
        serial,
        time_ms,
        int(round(azm_deg * 100.0)) % 36000,   # phương vị, đơn vị 0.01 độ
        int(round(range_m * 10.0)),            # cự ly, đơn vị 0.1 mét
        0, 0, 0, 0, 0,                         # 5 trường IFF
        12,                                    # numCX
        0,                                     # azmStart
        0,                                     # azmStop
        0,                                     # numLoseTotal
        amplitude,                             # amplitudeAverage
        amplitude,                             # amplitudeCenter
        0,                                     # rangeStart
        0,                                     # doplerStart
        0, 0, 0, 0, 0,                         # reserved01..05
        0,                                     # CheckSum
    ]
    assert len(words) == WORDS_PLOT
    return struct.pack("<{}I".format(WORDS_PLOT), *words)


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="127.0.0.1", help="địa chỉ đích")
    ap.add_argument("--port-v", type=int, default=6001, help="cổng RAW_V")
    ap.add_argument("--port-plot", type=int, default=6004,
                    help="cổng PlotTC (mặc định 6004, khớp dòng Plot)")
    ap.add_argument("--period", type=float, default=2.5,
                    help="chu kỳ gửi RAW_V, mili giây (mặc định 2.5)")
    ap.add_argument("--rpm", type=float, default=6.0,
                    help="tốc độ vòng quét, vòng/phút (mặc định 6)")
    ap.add_argument("--zfbeat", type=int, default=32768,
                    help="hệ số căn chỉnh biên độ, phải khớp tab Tham số")
    ap.add_argument("--no-video", action="store_true",
                    help="chỉ gửi PlotTC — bộ bám sẽ không có ai chốt sổ vòng "
                         "quét, chỉ dùng khi đang xem riêng lớp điểm dấu")
    ap.add_argument("--jitter-m", type=float, default=6.0,
                    help="sai số cự ly rắc vào mỗi điểm dấu (mét)")
    ap.add_argument("--jitter-deg", type=float, default=0.4,
                    help="sai số phương vị rắc vào mỗi điểm dấu (độ)")
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dest_v = (args.host, args.port_v)
    dest_p = (args.host, args.port_plot)

    period = args.period / 1000.0
    step_deg = 360.0 * (args.rpm / 60.0) * period
    scale = max(1, args.zfbeat // 256)

    targets = build_targets()

    print("Gửi RAW_V tới {}:{} và PlotTC tới {}:{}"
          .format(args.host, args.port_v, args.host, args.port_plot))
    print("  {:.1f} vòng/phút, {} mục tiêu".format(args.rpm, len(targets)))
    for t in targets:
        print("    {}: bắt đầu {:.0f}° / {:.0f} m, {:.1f} m/s, chu kỳ {}"
              .format(t.name, t.azm, t.range_m, t.speed,
                      " ".join("{}x{}".format("có" if p else "mất", n)
                               for p, n in t.pattern)))

    serial_v = 1
    serial_p = 1
    azimuth_deg = 0.0

    next_send = time.perf_counter()
    reported = next_send
    sent_plots = 0

    try:
        while True:
            az_enc = int(azimuth_deg * AZIMUTH_STEPS / 360.0) % AZIMUTH_STEPS

            if not args.no_video:
                video = [random.randint(NOISE_MIN, NOISE_MAX) * scale
                         for _ in range(BINS)]
                sock.sendto(build_raw_v(serial_v, az_enc, video), dest_v)
                serial_v += 1

            time_ms = int((time.time() % 86400) * 1000.0)

            for t in targets:
                t.advance(period)

                # Chùm tia vừa quét qua mục tiêu: dấu hiệu là hiệu góc đổi từ âm
                # sang không âm. Xét theo hiệu góc chứ không theo giá trị tuyệt
                # đối để mục tiêu nằm quanh mốc bắc cũng nhận ra đúng một lần.
                delta = wrap_delta_deg(azimuth_deg - t.azm)
                crossed = (t.prev_delta is not None
                           and t.prev_delta < 0.0 <= delta
                           and abs(delta - t.prev_delta) < 180.0)
                t.prev_delta = delta
                if not crossed:
                    continue

                visible = t.visible()
                t.scan += 1
                if not visible:
                    continue

                azm = (t.azm + random.gauss(0.0, args.jitter_deg)) % 360.0
                rng = max(0.0, t.range_m + random.gauss(0.0, args.jitter_m))
                amp = random.randint(600, 1400)
                sock.sendto(build_plot(serial_p, time_ms, azm, rng, amp), dest_p)
                serial_p += 1
                sent_plots += 1

            azimuth_deg = (azimuth_deg + step_deg) % 360.0

            now = time.perf_counter()
            if now - reported >= 5.0:
                reported = now
                print("  đã gửi {} điểm dấu, {} gói RAW_V"
                      .format(sent_plots, serial_v - 1))

            # Nhịp theo mốc tuyệt đối chứ không ngủ đúng `period`: ngủ tương đối
            # thì sai số của mỗi lần ngủ cộng dồn lại, chạy vài phút là tốc độ
            # vòng quét lệch hẳn khỏi con số đã đặt.
            next_send += period
            delay = next_send - time.perf_counter()
            if delay > 0:
                time.sleep(delay)
            else:
                next_send = time.perf_counter()
    except KeyboardInterrupt:
        print("\nĐã dừng — gửi tổng cộng {} điểm dấu.".format(sent_plots))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
