#!/usr/bin/env python3
import argparse
from collections import Counter
import csv
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description="给单次采集 CSV 添加九宫格位置并追加到汇总 CSV")
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--position", required=True)
    parser.add_argument("--include-quality", default="OK", choices=["OK", "all"],
                        help="默认只追加 quality=OK 的有效帧；传 all 保留全部帧")
    parser.add_argument("--limit", type=int, default=-1,
                        help="最多追加多少行；负数表示不限制")
    args = parser.parse_args()

    input_path = Path(args.input)
    output_path = Path(args.output)
    if not input_path.exists():
        raise SystemExit(f"CSV 不存在: {input_path}")

    rows = list(csv.DictReader(input_path.open("r", encoding="utf-8", newline="")))
    if not rows:
        print("[GRID] 单次 CSV 没有测量行")
        return

    fields = list(rows[0].keys())
    if "grid_position" not in fields:
        fields.insert(0, "grid_position")

    reason_counts = Counter(row.get("quality", "") for row in rows)
    filtered_rows = []
    for row in rows:
        if args.include_quality != "all" and row.get("quality") != args.include_quality:
            continue
        if not row.get("grid_position"):
            row["grid_position"] = args.position
        filtered_rows.append(row)

    if not filtered_rows:
        reasons = ", ".join(f"{reason or 'EMPTY'}={count}" for reason, count in reason_counts.items())
        print(f"[GRID] 本轮没有可追加的有效帧: {reasons}")
        return
    if args.limit >= 0:
        filtered_rows = filtered_rows[:args.limit]
    if not filtered_rows:
        print("[GRID] 目标有效帧数量已满足，本轮不再追加")
        return

    output_path.parent.mkdir(parents=True, exist_ok=True)
    needs_header = not output_path.exists() or output_path.stat().st_size == 0
    if not needs_header:
        with output_path.open("r", encoding="utf-8", newline="") as f:
            existing_fields = next(csv.reader(f), [])
        if existing_fields != fields:
            raise SystemExit(
                "汇总 CSV 表头不匹配，请换一个输出文件或删除旧文件后重采集: "
                f"{output_path}"
            )

    with output_path.open("a", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        if needs_header:
            writer.writeheader()
        writer.writerows(filtered_rows)

    print(f"[GRID] 本轮输入 {len(rows)} 行，追加 {len(filtered_rows)} 行到 {output_path}")


if __name__ == "__main__":
    main()
