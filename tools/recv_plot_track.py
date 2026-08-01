#!/usr/bin/env python3
"""Thu và giải mã gói PlotTC / Track do phần mềm gửi ra.

Đóng vai "hệ thống khác" để kiểm tra phía gửi: mở cổng, giải mã theo đúng bảng
mô tả giao thức rồi in ra. Cũng dùng được lúc lắp đặt để xem đầu bên kia có
nhận đúng không.

    python3 tools/recv_plot_track.py                  # nghe cả 6101 và 6102
    python3 tools/recv_plot_track.py --port 6101      # chỉ một cổng
    python3 tools/recv_plot_track.py --raw            # in thêm dạng thô

Dừng bằng Ctrl+C.
"""

import argparse
import select
import socket
import struct
import sys

HEADER = 0x2D2D2D2D
CAT_PLOT = 0x2021
CAT_TRACK = 0x2051
WORDS_PLOT = 26
WORDS_TRACK = 38

PLOT_FIELDS = [
    "Header", "Category", "Length", "Serial", "Time", "azm", "range",
    "iff_returned_mode", "iff_commander", "iff_flight_id", "iff_altitude",
    "iff_fuel_level", "numCX", "azmStart", "azmStop", "numLoseTotal",
    "amplitueAverage", "amplitueCenter", "rangeStart", "doplerStart",
    "reserved01", "reserved02", "reserved03", "reserved04", "reserved05",
    "CheckSum",
]

TRACK_FIELDS = [
    "Header", "Category", "Length", "Serial", "Time", "track_type",
    "track_status", "track_id", "track_top", "track_azm", "track_range",
    "track_velocity", "track_heading", "track_iff_returned_mode",
    "track_iff_commander", "track_iff_flight_id", "track_iff_altitude",
    "track_iff_fuel_level", "track_lat", "track_lng", "track_classify",
    "track_altitude_manual", "track_amplitue", "reserved01",
    "track_window_azm1", "track_window_azm2", "track_window_range1",
    "track_window_range2", "reserved02", "reserved03", "reserved04",
    "reserved05", "reserved06", "reserved07", "reserved08", "reserved09",
    "reserved10", "CheckSum",
]

# Chỉ số hai trường kiểu float trong gói Track — cả bốn loại gói chỉ có đúng
# hai chỗ này không phải số nguyên.
TRACK_FLOAT_WORDS = (18, 19)

STATUS_NAMES = {
    1: "khoi tao", 2: "khang dinh", 3: "dang bam",
    4: "tam mat", 5: "ngoai suy", 6: "XOA",
}


def words(data):
    n = len(data) // 4
    return list(struct.unpack("<{}I".format(n), data[: n * 4]))


def as_float(word):
    return struct.unpack("<f", struct.pack("<I", word))[0]


def ms_of_day(v):
    v %= 86400000
    return "{:02d}:{:02d}:{:02d}.{:03d}".format(
        v // 3600000, v // 60000 % 60, v // 1000 % 60, v % 1000)


def show_plot(w, raw):
    print("PlotTC  #{:<6} {}  PV {:.2f}deg  CL {:.1f} m  "
          "n={} lose={} r0={} dop={} ampAvg={} ampC={}"
          .format(w[3], ms_of_day(w[4]), w[5] / 100.0, w[6] / 10.0,
                  w[12], w[15], w[18], w[19], w[16], w[17]))
    if raw:
        for i, name in enumerate(PLOT_FIELDS):
            print("    [{:2d}] {:<22} {}".format(i, name, w[i]))


def show_track(w, raw):
    lat = as_float(w[18])
    lng = as_float(w[19])
    print("Track   #{:<6} {}  top={:<4} {:<10} PV {:.2f}deg  CL {:.1f} m  "
          "V {:.1f} m/s  H {:.2f}deg  ({:.6f}, {:.6f})  loai={} DC={}"
          .format(w[3], ms_of_day(w[4]), w[8],
                  STATUS_NAMES.get(w[6], "?{}".format(w[6])),
                  w[9] / 100.0, w[10] / 10.0, w[11] / 10.0, w[12] / 100.0,
                  lat, lng, w[20], w[21]))
    if raw:
        for i, name in enumerate(TRACK_FIELDS):
            value = as_float(w[i]) if i in TRACK_FLOAT_WORDS else w[i]
            print("    [{:2d}] {:<24} {}".format(i, name, value))


def check(w, want_words, want_cat, name):
    """Kiểm tra phần khung của gói. Trả về chuỗi lỗi, hoặc None nếu đúng."""
    if len(w) != want_words:
        return "{}: {} tu, cho doi {}".format(name, len(w), want_words)
    if w[0] != HEADER:
        return "{}: Header 0x{:08x}, cho doi 0x{:08x}".format(name, w[0], HEADER)
    if w[2] != want_words * 4:
        return "{}: Length {}, cho doi {}".format(name, w[2], want_words * 4)
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="0.0.0.0",
                    help="địa chỉ nghe (0.0.0.0 = mọi card, nhận được cả broadcast)")
    ap.add_argument("--port", type=int, action="append",
                    help="cổng nghe, lặp lại được (mặc định 6101 và 6102)")
    ap.add_argument("--raw", action="store_true", help="in đủ từng trường")
    args = ap.parse_args()

    ports = args.port or [6101, 6102]
    socks = []
    for p in ports:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((args.host, p))
        socks.append(s)
        print("Nghe {}:{}".format(args.host, p))

    counts = {"plot": 0, "track": 0, "la": 0}
    try:
        while True:
            ready, _, _ = select.select(socks, [], [], 1.0)
            for s in ready:
                data, addr = s.recvfrom(65535)
                w = words(data)
                if not w or w[0] != HEADER:
                    counts["la"] += 1
                    print("goi la tu {}: {} byte".format(addr, len(data)))
                    continue

                if w[1] == CAT_PLOT:
                    err = check(w, WORDS_PLOT, CAT_PLOT, "PlotTC")
                    if err:
                        print("SAI KHUNG " + err)
                        continue
                    counts["plot"] += 1
                    show_plot(w, args.raw)
                elif w[1] == CAT_TRACK:
                    err = check(w, WORDS_TRACK, CAT_TRACK, "Track")
                    if err:
                        print("SAI KHUNG " + err)
                        continue
                    counts["track"] += 1
                    show_track(w, args.raw)
                else:
                    counts["la"] += 1
                    print("Category la 0x{:x} tu {}".format(w[1], addr))
    except KeyboardInterrupt:
        print("\nDa nhan {} PlotTC, {} Track, {} goi la."
              .format(counts["plot"], counts["track"], counts["la"]))

    return 0


if __name__ == "__main__":
    sys.exit(main())
