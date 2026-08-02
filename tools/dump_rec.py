#!/usr/bin/env python3
"""Soi một file ghi lưu .rec của phần mềm.

Đọc 64 byte header rồi duyệt phần Data, đối chiếu số bản ghi đếm được với số
khai trong header. Dùng để kiểm chứng phía ghi mà không phải mở giao diện.

    python3 tools/dump_rec.py build/records/2026/08/02/dat_20260802_143012.rec
    python3 tools/dump_rec.py --list 20 <file>     # in 20 bản ghi đầu
    python3 tools/dump_rec.py --scan build/records # duyệt cả thư mục

Header đúng theo mô tả giai đoạn 5; hai ô cuối (số bản ghi nền tạp, phiên bản
định dạng) lấy từ phần dự phòng — xem src/recordfile.h.
"""

import argparse
import datetime
import pathlib
import struct
import sys

MAGIC = 0x6969CAFE
KIND_RAW = 0xCAFE1122
KIND_PROC = 0xCAFE3344

HEADER_BYTES = 64
REC_HEAD_BYTES = 16

KIND_NAME = {KIND_RAW: "dữ liệu gốc", KIND_PROC: "dữ liệu đã qua xử lý"}
TYPE_NAME = {1: "RAW_V", 2: "RAW_P", 3: "Video", 4: "Plot", 5: "Track", 6: "Khác"}

# Cỡ hợp lệ của từng loại bản ghi; None = không cố định.
TYPE_SIZE = {1: 4124, 2: 288, 3: 1028, 4: 104, 5: 152, 6: None}


def stamp(sec):
    if not sec:
        return "-"
    return datetime.datetime.fromtimestamp(sec).strftime("%Y/%m/%d %H:%M:%S")


def read_header(data):
    if len(data) < HEADER_BYTES:
        return None
    w = struct.unpack("<16I", data[:HEADER_BYTES])
    if w[0] != MAGIC:
        return None
    return {
        "kind": w[1], "start": w[2], "end": w[3], "total": w[4],
        "rawV": w[5], "rawP": w[6], "plot": w[7], "track": w[8],
        "other": w[9], "video": w[10], "version": w[11],
    }


def dump(path, list_n):
    raw = path.read_bytes()
    head = read_header(raw)
    if head is None:
        print(f"{path}: KHÔNG phải file ghi lưu (sai định danh)")
        return False

    print(f"== {path}")
    print(f"   Phân loại : {KIND_NAME.get(head['kind'], hex(head['kind']))}")
    print(f"   Bắt đầu   : {stamp(head['start'])}")
    print(f"   Kết thúc  : {stamp(head['end'])}")
    print(f"   Dung lượng: {len(raw):,} byte   (định dạng v{head['version']})")
    print(f"   Header khai: tổng {head['total']:,}  RAW_V {head['rawV']:,}  "
          f"RAW_P {head['rawP']:,}  Video {head['video']:,}  "
          f"Plot {head['plot']:,}  Track {head['track']:,}  Khác {head['other']:,}")

    counts = {}
    at = HEADER_BYTES
    n = 0
    bad = []
    first_ms = last_ms = None

    while at + REC_HEAD_BYTES <= len(raw):
        rtype, length, tms = struct.unpack("<IIQ", raw[at:at + REC_HEAD_BYTES])
        at += REC_HEAD_BYTES
        if at + length > len(raw):
            bad.append(f"bản ghi #{n} cụt: cần {length} byte, còn {len(raw) - at}")
            break
        want = TYPE_SIZE.get(rtype, "?")
        if want is not None and want != "?" and length != want:
            bad.append(f"bản ghi #{n} loại {rtype}: dài {length}, chờ {want}")
        if want == "?":
            bad.append(f"bản ghi #{n}: loại lạ {rtype}")

        if list_n and n < list_n:
            print(f"   [{n:5d}] {TYPE_NAME.get(rtype, rtype):6s} "
                  f"{length:6d} byte  {datetime.datetime.fromtimestamp(tms / 1000)}")

        counts[rtype] = counts.get(rtype, 0) + 1
        first_ms = first_ms if first_ms is not None else tms
        last_ms = tms
        at += length
        n += 1

    print(f"   Đếm được  : tổng {n:,}  " + "  ".join(
        f"{TYPE_NAME.get(t, t)} {c:,}" for t, c in sorted(counts.items())))
    if first_ms is not None:
        print(f"   Mốc thời gian bản ghi: {stamp(first_ms / 1000)} .. "
              f"{stamp(last_ms / 1000)}  ({(last_ms - first_ms) / 1000:.1f} giây)")
    if at != len(raw):
        bad.append(f"còn thừa {len(raw) - at} byte ở cuối file")

    ok = True
    if n != head["total"]:
        print(f"   !! header khai {head['total']:,} bản ghi, đếm được {n:,}")
        ok = False
    for msg in bad:
        print(f"   !! {msg}")
        ok = False
    if ok:
        print("   OK — header khớp với nội dung")
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path", help="file .rec, hoặc thư mục khi dùng --scan")
    ap.add_argument("--list", type=int, default=0, metavar="N",
                    help="in N bản ghi đầu tiên")
    ap.add_argument("--scan", action="store_true",
                    help="duyệt mọi file .rec trong thư mục")
    args = ap.parse_args()

    root = pathlib.Path(args.path)
    files = sorted(root.rglob("*.rec")) if args.scan else [root]
    if not files:
        print("không có file .rec nào")
        return 1

    all_ok = True
    for f in files:
        all_ok &= dump(f, args.list)
        print()
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
