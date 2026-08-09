#!/usr/bin/env python3
"""Tạo giả RAW_V và RAW_P đồng bộ, **một mục tiêu duy nhất**, để kiểm tra thuật toán.

Khác fake_radar.py ở chỗ đây là một bài đo chứ không phải một cảnh mô phỏng: chỉ
một mục tiêu, chuyển động biết trước từng mét, và chu kỳ có tiêu / mất tiêu cố
định theo vòng quét. Nhờ vậy mọi sai lệch nhìn thấy trên màn hình đều quy được
về thuật toán chứ không phải về công cụ — mặc định **không** rắc nhiễu vào cự ly
lẫn phương vị, và **không** làm rơi xung giữa chùm.

Mục tiêu theo mô tả giai đoạn 7:

  - Bay vòng tròn quanh tâm đài, xuôi kim đồng hồ, vận tốc dài 2.5 m/s.
  - Bắt đầu ở phương vị 30°, cự ly 500 m. Bay vòng tròn nên cự ly giữ nguyên
    500 m suốt phiên — mọi thay đổi cự ly nhìn thấy trên màn hình đều là sai
    số của thuật toán.
  - Chu kỳ có tiêu / mất tiêu theo vòng quét, lặp lại sau 22 vòng:
        vòng  1        có tiêu
        vòng  2        mất tiêu
        vòng  3 -  8   có tiêu   (6 vòng)
        vòng  9 - 12   mất tiêu  (4 vòng)
        vòng 13 - 18   có tiêu   (6 vòng)
        vòng 19 - 22   mất tiêu  (4 vòng)

Vòng "mất tiêu" là mất hẳn: không plot trong RAW_P, và cũng không có vệt sáng
trong RAW_V — nhìn màn hình là thấy đúng lúc bộ bám chuyển sang ngoại suy. Muốn
giữ vệt nền tạp lại (mô phỏng tầng phát hiện trượt chứ không phải mục tiêu biến
mất) thì thêm --video-always.

RAW_P vẫn được gửi mỗi chu kỳ kể cả khi không có plot nào: phần mềm chốt sổ vòng
quét theo phương vị của dòng RAW_P, ngắt dòng đó là bộ bám đứng lại.

Cự ly quy ra ô theo Rmax của đài (mặc định 1218 m, ứng với Fs=1, B=154,
Tc=2500). Tham số bên tab "Tham số" khác đi thì phải truyền --rmax cho khớp, nếu
không cự ly hiện trên màn hình sẽ không phải 500 m.

    python3 tools/fake_one_target.py
    python3 tools/fake_one_target.py --host 127.0.0.1 --port-v 6001 --port-p 6002
    python3 tools/fake_one_target.py --jitter-cell 1 --dropout 0.04

Dừng bằng Ctrl+C.
"""

import argparse
import math
import random
import socket
import struct
import sys
import time

# --- RAW_V, đúng như mô tả giao thức giai đoạn 3 ---------------------------
HEADER_V = 0xB4B3B2B1
CATEGORY_V = 0x20180
BINS = 1024
WORDS_V = 6 + BINS + 1          # 1031 từ, khớp trường Length
LENGTH_V = WORDS_V * 4

# --- RAW_P -----------------------------------------------------------------
HEADER_P = 0xC4C3C2C1
CATEGORY_P = 0x30180
PLOTS = 64                      # Data_P[64]
MAX_PLOTS = PLOTS - 1           # Data_P[0] là từ tiêu đề chu kỳ
WORDS_P = 7 + PLOTS + 1         # 72 từ
LENGTH_P = WORDS_P * 4

AZIMUTH_STEPS = 4096
RANGE_CELLS = 1024

# Nền tạp trên thang 256; ZFbeat mặc định 32768 nên giá trị thô = mức * 128.
NOISE_MIN, NOISE_MAX = 5, 8
TARGET_MIN, TARGET_MAX = 150, 180

# Chu kỳ có tiêu / mất tiêu, tính theo vòng quét. Tổng 22 vòng rồi lặp lại.
BLIP_PATTERN = [(True, 1), (False, 1), (True, 6), (False, 4), (True, 6), (False, 4)]


def clamp(v, lo, hi):
    return lo if v < lo else (hi if v > hi else v)


def wrap_delta_deg(a):
    """Chênh lệch góc theo đường ngắn nhất, trong (-180, 180]."""
    return (a + 180.0) % 360.0 - 180.0


def expand_pattern(pattern):
    """Trải (có tiêu?, số vòng) thành một dãy True/False dài đúng số vòng."""
    blips = []
    for present, count in pattern:
        blips.extend([present] * count)
    return blips


class CircleTarget:
    """Mục tiêu bay vòng tròn quanh tâm đài, xuôi chiều kim đồng hồ.

    Vị trí giữ ở dạng cực vì cả hai loại gói đều phát biểu theo hệ đó, và vì bay
    vòng tròn quanh chính tâm đài thì cự ly là hằng số — không có gì phải tính
    trong hệ Đề-các rồi đổi ngược lại.
    """

    def __init__(self, azm_deg, range_m, speed_ms, rmax_m, pulses, dopler):
        self.azm = float(azm_deg)
        self.range_m = float(range_m)
        self.speed = float(speed_ms)
        self.rmax_m = float(rmax_m)
        self.pulses = pulses
        self.dopler = dopler

        self.blips = expand_pattern(BLIP_PATTERN)
        self.scan = 0             # số vòng quét đã đi qua, 0 là vòng thứ nhất
        self.visible = self.blips[0]

        # Độ lệch ô cự ly của lần quét qua này. Chọn một lần cho cả chùm chứ
        # không random từng xung: thuật toán xét duyệt plot vào chùm so với
        # **xung đầu chùm** với tiêu chuẩn ±1 ô, random từng xung thì xung đầu
        # rơi vào -1 là mọi xung +1 bị loại, chùm bị cắt đôi thành hai điểm dấu.
        self.cell_offset = 0

    def advance(self, dt):
        # Vận tốc dài đổi ra vận tốc góc: omega = v / r.
        self.azm = (self.azm + math.degrees(self.speed / self.range_m) * dt) % 360.0

    def begin_pass(self, jitter_cell):
        """Chùm tia bắt đầu quét qua: chốt xem vòng này có tiêu hay không."""
        self.visible = self.blips[self.scan % len(self.blips)]
        self.cell_offset = random.randint(-jitter_cell, jitter_cell) if jitter_cell else 0

    def end_pass(self):
        """Chùm tia vừa đi khỏi: sang vòng quét kế tiếp."""
        self.scan += 1

    def scan_number(self):
        """Số thứ tự vòng quét trong chu kỳ 22 vòng, đếm từ 1."""
        return self.scan % len(self.blips) + 1

    def cell(self):
        c = int(round(self.range_m * RANGE_CELLS / self.rmax_m))
        return clamp(c + self.cell_offset, 0, RANGE_CELLS - 1)

    def half_beam_deg(self, step_deg):
        # Bề rộng chùm tia suy ngược từ số xung mong muốn: mỗi chu kỳ phương vị
        # nhích step_deg, muốn N xung thì chùm rộng N nấc.
        return self.pulses * step_deg / 2.0


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
                    help="tốc độ vòng quét tiêu chuẩn, vòng/phút (mặc định 6)")
    ap.add_argument("--zfbeat", type=int, default=32768,
                    help="hệ số căn chỉnh biên độ, phải khớp tab Tham số")
    ap.add_argument("--rmax", type=float, default=1218.0,
                    help="cự ly tối đa của đài, mét — phải khớp tab Cài đặt "
                         "(mặc định 1218, ứng với Fs=1 B=154 Tc=2500)")
    ap.add_argument("--azm", type=float, default=30.0,
                    help="phương vị xuất phát, độ (mặc định 30)")
    ap.add_argument("--range", type=float, default=500.0, dest="range_m",
                    help="cự ly của vòng bay, mét (mặc định 500)")
    ap.add_argument("--speed", type=float, default=2.5,
                    help="vận tốc dài, m/s (mặc định 2.5)")
    ap.add_argument("--pulses", type=int, default=20,
                    help="số xung mỗi lần chùm tia quét qua (mặc định 20) — "
                         "phải nằm giữa CX_MIN và CX_MAX bên cửa sổ tham số "
                         "tâm chùm")
    ap.add_argument("--dopler", type=int, default=8,
                    help="mức dopler của mục tiêu, 0..31 (mặc định 8) — phải "
                         "nằm giữa CX_DOPLER_MIN và CX_DOPLER_MAX")
    ap.add_argument("--no-video", action="store_true",
                    help="chỉ gửi RAW_P (để nhìn riêng lớp điểm dấu)")
    ap.add_argument("--video-always", action="store_true",
                    help="giữ vệt nền tạp của mục tiêu cả trong vòng mất tiêu")
    ap.add_argument("--dropout", type=float, default=0.0,
                    help="tỉ lệ xung bị mất giữa chùm (mặc định 0, sạch hoàn "
                         "toàn) — nâng lên để tiêu chuẩn CX_NUM_LOSE có việc")
    ap.add_argument("--jitter-cell", type=int, default=0,
                    help="sai số ô cự ly rắc vào mỗi lần quét qua, ± ô "
                         "(mặc định 0)")
    args = ap.parse_args()

    if not 0 <= args.dopler <= 31:
        print("dopler phải nằm trong 0..31", file=sys.stderr)
        return 2
    if args.range_m <= 0.0 or args.range_m > args.rmax:
        print("cự ly phải nằm trong (0, {:.0f}] mét".format(args.rmax),
              file=sys.stderr)
        return 2

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dest_v = (args.host, args.port_v)
    dest_p = (args.host, args.port_p)

    period = args.period / 1000.0
    step_deg = 360.0 * (args.rpm / 60.0) * period      # phương vị nhích mỗi chu kỳ
    scale = max(1, args.zfbeat // 256)                 # mức 0..255 -> giá trị thô

    target = CircleTarget(args.azm, args.range_m, args.speed, args.rmax,
                          args.pulses, args.dopler)
    half = target.half_beam_deg(step_deg)

    print("Gửi RAW_V tới {}:{} và RAW_P tới {}:{}"
          .format(args.host, args.port_v, args.host, args.port_p))
    print("  {:.1f} vòng/phút ({:.1f} s một vòng), {:.0f} gói/giây mỗi loại"
          .format(args.rpm, 60.0 / args.rpm, 1.0 / period))
    print("  một mục tiêu: vòng tròn xuôi kim đồng hồ, {:.1f} m/s, "
          "bắt đầu {:.0f}° / {:.0f} m (ô {})"
          .format(args.speed, args.azm, args.range_m, target.cell()))
    print("  chùm tia rộng {:.2f}°, {} xung mỗi lần quét qua"
          .format(half * 2.0, args.pulses))
    print("  chu kỳ {} vòng: {}"
          .format(len(target.blips),
                  " ".join("{}x{}".format("có" if p else "mất", n)
                           for p, n in BLIP_PATTERN)))

    serial = 1
    azimuth_deg = 0.0
    cycle_count = 0
    was_lit = False

    next_send = time.perf_counter()

    try:
        while True:
            az_enc = int(azimuth_deg * AZIMUTH_STEPS / 360.0) % AZIMUTH_STEPS

            video = [random.randint(NOISE_MIN, NOISE_MAX) * scale
                     for _ in range(BINS)]
            plots = []

            lit = abs(wrap_delta_deg(azimuth_deg - target.azm)) <= half
            if lit and not was_lit:
                target.begin_pass(args.jitter_cell)
                print("  vòng {:2d}/{}: {}   phương vị {:6.2f}°  cự ly {:.0f} m"
                      .format(target.scan_number(), len(target.blips),
                              "có tiêu " if target.visible else "mất tiêu",
                              target.azm, target.range_m))
            elif not lit and was_lit:
                target.end_pass()
            was_lit = lit

            if lit and (target.visible or args.video_always):
                cell = target.cell()

                # Vệt nền tạp rộng hơn một ô: xung phát hiện là kết quả của tầng
                # xử lý phía sau, còn nền tạp là tín hiệu thô trải trên vài ô.
                for k in range(max(0, cell - 2), min(BINS, cell + 3)):
                    video[k] = random.randint(TARGET_MIN, TARGET_MAX) * scale

                if target.visible and random.random() >= args.dropout:
                    plots.append((cell, random.randint(12000, 17000), args.dopler))

            if not args.no_video:
                sock.sendto(build_raw_v(serial, az_enc, video), dest_v)
            # RAW_P gửi mọi chu kỳ, kể cả chu kỳ rỗng: phần mềm chốt sổ vòng quét
            # theo phương vị của dòng này.
            sock.sendto(build_raw_p(serial, az_enc, cycle_count, plots), dest_p)

            target.advance(period)

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
    except KeyboardInterrupt:
        print("\nĐã dừng sau {} gói mỗi loại, {} vòng quét."
              .format(serial - 1, target.scan))

    return 0


if __name__ == "__main__":
    sys.exit(main())
