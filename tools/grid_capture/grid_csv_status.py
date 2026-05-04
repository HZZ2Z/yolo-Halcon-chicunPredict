#!/usr/bin/env python3
import argparse
import csv
import math
from pathlib import Path


def to_float(row, key):
    try:
        return float(row.get(key, ""))
    except ValueError:
        return float("nan")


def mean(values):
    values = [v for v in values if math.isfinite(v)]
    return sum(values) / len(values) if values else float("nan")


def fmt(value):
    return "N/A" if not math.isfinite(value) else f"{value:.6f}"


def load_rows(path):
    if not path.exists():
        return []
    with path.open("r", encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))


def main():
    parser = argparse.ArgumentParser(description="统计九宫格 CSV 中某位置的有效帧数量")
    parser.add_argument("--input", required=True)
    parser.add_argument("--position", required=True)
    parser.add_argument("--quality", default="OK", help="默认统计 OK；传 all 表示全部")
    parser.add_argument("--field", choices=["count", "summary"], default="summary")
    args = parser.parse_args()

    rows = []
    for row in load_rows(Path(args.input)):
        if row.get("grid_position") != args.position:
            continue
        if args.quality != "all" and row.get("quality") != args.quality:
            continue
        rows.append(row)

    count = len(rows)
    raw_mm_mean = mean([to_float(row, "raw_mm") for row in rows])
    mm_mean = mean([to_float(row, "mm") for row in rows])
    sigma_mean = mean([to_float(row, "sigma") for row in rows])

    if args.field == "count":
        print(count)
        return

    print(
        f"count={count} raw_mm_mean={fmt(raw_mm_mean)} "
        f"mm_mean={fmt(mm_mean)} sigma_mean={fmt(sigma_mean)}"
    )


if __name__ == "__main__":
    main()
