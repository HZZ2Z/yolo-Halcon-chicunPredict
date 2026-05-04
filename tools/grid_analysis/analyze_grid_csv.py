#!/usr/bin/env python3
import argparse
import csv
import math
from collections import defaultdict
from pathlib import Path


POSITIONS = ["LT", "CT", "RT", "LC", "C", "RC", "LB", "CB", "RB"]


def to_float(row, key, default=float("nan")):
    try:
        return float(row.get(key, ""))
    except ValueError:
        return default


def mean(values):
    values = [v for v in values if math.isfinite(v)]
    return sum(values) / len(values) if values else float("nan")


def std(values):
    values = [v for v in values if math.isfinite(v)]
    if len(values) < 2:
        return 0.0 if len(values) == 1 else float("nan")
    m = mean(values)
    return math.sqrt(sum((v - m) ** 2 for v in values) / (len(values) - 1))


def main():
    parser = argparse.ArgumentParser(description="统计九宫格测量 CSV 并生成残差补偿表")
    parser.add_argument("--input", required=True)
    parser.add_argument("--summary", default="measurements/grid_summary.csv")
    parser.add_argument("--residual", default="measurements/residual_grid.csv")
    parser.add_argument("--use-quality", default="OK",
                        help="只统计指定 quality，默认 OK；传 all 表示全部")
    args = parser.parse_args()

    input_path = Path(args.input)
    if not input_path.exists():
        raise SystemExit(f"输入 CSV 不存在: {input_path}")

    groups = defaultdict(list)
    with input_path.open("r", encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            pos = row.get("grid_position", "").strip()
            if not pos:
                continue
            if args.use_quality != "all" and row.get("quality") != args.use_quality:
                continue
            groups[pos].append(row)

    summary_path = Path(args.summary)
    residual_path = Path(args.residual)
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    residual_path.parent.mkdir(parents=True, exist_ok=True)

    summary_fields = [
        "grid_position", "count", "cx_mean", "cy_mean", "px_mean", "px_std",
        "mm_mean", "mm_std", "raw_mm_mean", "raw_mm_std", "mm_per_px_mean",
        "sigma_mean", "scans_mean", "true_mm", "error_mean", "correction_mm"
    ]
    residual_fields = ["grid_position", "x", "y", "correction_mm", "error_mm", "count"]

    summary_rows = []
    residual_rows = []
    for pos in POSITIONS:
        rows = groups.get(pos, [])
        if not rows:
            continue
        px = [to_float(r, "px") for r in rows]
        mm = [to_float(r, "mm") for r in rows]
        raw_mm = [to_float(r, "raw_mm") for r in rows]
        sigma = [to_float(r, "sigma") for r in rows]
        scans = [to_float(r, "scans") for r in rows]
        cx = [to_float(r, "cx") for r in rows]
        cy = [to_float(r, "cy") for r in rows]
        true_vals = [to_float(r, "true_mm") for r in rows if to_float(r, "true_mm") > 0]
        true_mm = mean(true_vals)
        mm_per_px = [
            m / p for m, p in zip(raw_mm, px)
            if math.isfinite(m) and math.isfinite(p) and p > 0 and m > 0
        ]
        error = mean(raw_mm) - true_mm if math.isfinite(true_mm) else float("nan")
        correction = -error if math.isfinite(error) else 0.0

        summary_rows.append({
            "grid_position": pos,
            "count": len(rows),
            "cx_mean": mean(cx),
            "cy_mean": mean(cy),
            "px_mean": mean(px),
            "px_std": std(px),
            "mm_mean": mean(mm),
            "mm_std": std(mm),
            "raw_mm_mean": mean(raw_mm),
            "raw_mm_std": std(raw_mm),
            "mm_per_px_mean": mean(mm_per_px),
            "sigma_mean": mean(sigma),
            "scans_mean": mean(scans),
            "true_mm": true_mm,
            "error_mean": error,
            "correction_mm": correction,
        })
        residual_rows.append({
            "grid_position": pos,
            "x": mean(cx),
            "y": mean(cy),
            "correction_mm": correction,
            "error_mm": error,
            "count": len(rows),
        })

    with summary_path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=summary_fields)
        writer.writeheader()
        writer.writerows(summary_rows)

    with residual_path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=residual_fields)
        writer.writeheader()
        writer.writerows(residual_rows)

    print(f"[GRID] summary: {summary_path}")
    print(f"[GRID] residual: {residual_path}")


if __name__ == "__main__":
    main()
