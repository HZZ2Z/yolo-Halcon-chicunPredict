#!/usr/bin/env python3
import argparse
import csv
import math
import statistics
from pathlib import Path


def as_float(value):
    try:
        number = float(value)
    except (TypeError, ValueError):
        return float("nan")
    return number


def mean(values):
    return statistics.mean(values) if values else float("nan")


def stdev(values):
    return statistics.stdev(values) if len(values) > 1 else 0.0


def fmt(value):
    return f"{value:.6f}" if math.isfinite(value) else "N/A"


def main():
    parser = argparse.ArgumentParser(description="汇总主链路残差补偿测试 CSV")
    parser.add_argument("--input", required=True)
    parser.add_argument("--true-mm", type=float, default=-1.0)
    parser.add_argument("--quality", default="OK",
                        help="默认只统计 quality=OK；传 all 统计所有可用测量行")
    args = parser.parse_args()

    path = Path(args.input)
    if not path.exists():
        raise SystemExit(f"[COMP_TEST] CSV 不存在: {path}")

    rows = list(csv.DictReader(path.open("r", encoding="utf-8", newline="")))
    usable = []
    for row in rows:
        if args.quality != "all" and row.get("quality") != args.quality:
            continue
        mm = as_float(row.get("raw_mm"))
        if not math.isfinite(mm) or mm <= 0.0:
            continue
        usable.append(row)

    if not usable:
        print(f"[COMP_TEST] input: {path}")
        print(f"[COMP_TEST] rows={len(rows)} usable=0 quality_filter={args.quality}")
        return

    true_mm = args.true_mm
    if true_mm <= 0.0:
        for row in usable:
            candidate = as_float(row.get("true_mm"))
            if math.isfinite(candidate) and candidate > 0.0:
                true_mm = candidate
                break

    corrected = [as_float(row.get("raw_mm")) for row in usable]
    corrections = [as_float(row.get("correction_mm")) for row in usable]
    before = [
        mm - corr if math.isfinite(corr) else mm
        for mm, corr in zip(corrected, corrections)
    ]
    sigma = [as_float(row.get("sigma")) for row in usable]
    scans = [as_float(row.get("scans")) for row in usable]

    print(f"[COMP_TEST] input: {path}")
    print(f"[COMP_TEST] rows={len(rows)} usable={len(usable)} quality_filter={args.quality}")
    print(f"[COMP_TEST] true_mm={fmt(true_mm)}")
    print(f"[COMP_TEST] correction_mm mean={fmt(mean(corrections))} std={fmt(stdev(corrections))}")
    print(f"[COMP_TEST] before_mm mean={fmt(mean(before))} std={fmt(stdev(before))}")
    print(f"[COMP_TEST] after_mm  mean={fmt(mean(corrected))} std={fmt(stdev(corrected))}")
    if true_mm > 0.0:
        before_errors = [v - true_mm for v in before]
        after_errors = [v - true_mm for v in corrected]
        print(f"[COMP_TEST] before_error mean={fmt(mean(before_errors))} abs_mean={fmt(mean([abs(v) for v in before_errors]))}")
        print(f"[COMP_TEST] after_error  mean={fmt(mean(after_errors))} abs_mean={fmt(mean([abs(v) for v in after_errors]))}")
    print(f"[COMP_TEST] sigma_mean={fmt(mean(sigma))} scans_mean={fmt(mean(scans))}")
    print("[COMP_TEST] note: before_mm is reconstructed as raw_mm - correction_mm")


if __name__ == "__main__":
    main()
