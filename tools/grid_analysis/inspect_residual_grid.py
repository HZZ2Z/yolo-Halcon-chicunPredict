#!/usr/bin/env python3
import argparse
import csv
import math
from pathlib import Path


EXPECTED_POSITIONS = ["LT", "CT", "RT", "LC", "C", "RC", "LB", "CB", "RB"]


def as_float(value):
    try:
        number = float(value)
    except (TypeError, ValueError):
        return float("nan")
    return number


def main():
    parser = argparse.ArgumentParser(description="检查九宫格残差补偿表完整性")
    parser.add_argument("--input", required=True)
    args = parser.parse_args()

    path = Path(args.input)
    if not path.exists():
        raise SystemExit(f"[COMP_TEST] 残差补偿表不存在: {path}")

    rows = list(csv.DictReader(path.open("r", encoding="utf-8", newline="")))
    if not rows:
        raise SystemExit(f"[COMP_TEST] 残差补偿表为空: {path}")

    positions = [row.get("grid_position", "") for row in rows if row.get("grid_position", "")]
    present = [pos for pos in EXPECTED_POSITIONS if pos in positions]
    missing = [pos for pos in EXPECTED_POSITIONS if pos not in positions]
    corrections = [as_float(row.get("correction_mm")) for row in rows]
    finite_corrections = [v for v in corrections if math.isfinite(v)]
    max_abs = max((abs(v) for v in finite_corrections), default=float("nan"))

    print(f"[COMP_TEST] residual: {path}")
    print(f"[COMP_TEST] samples={len(rows)} positions={' '.join(present) if present else 'N/A'}")
    if missing:
        print(f"[COMP_TEST] WARNING: missing_positions={' '.join(missing)}")
    if math.isfinite(max_abs):
        print(f"[COMP_TEST] max_abs_correction_mm={max_abs:.6f}")
        if max_abs > 0.5:
            print("[COMP_TEST] WARNING: correction exceeds 0.50 mm; use this table for diagnosis first")


if __name__ == "__main__":
    main()
