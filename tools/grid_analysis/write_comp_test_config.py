#!/usr/bin/env python3
import argparse
from pathlib import Path


def replace_or_insert(lines, key, value):
    prefix = f"  {key}:"
    for idx, line in enumerate(lines):
        if line.startswith(prefix):
            lines[idx] = f"  {key}: {value}\n"
            return
    insert_at = len(lines)
    for idx, line in enumerate(lines):
        if line.startswith("  show_window:"):
            insert_at = idx
            break
    lines.insert(insert_at, f"  {key}: {value}\n")


def main():
    parser = argparse.ArgumentParser(description="生成主链路残差补偿测试临时配置")
    parser.add_argument("--base", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--csv", required=True)
    parser.add_argument("--true-mm", required=True)
    parser.add_argument("--frames", required=True, type=int)
    parser.add_argument("--residual", default="")
    args = parser.parse_args()

    base = Path(args.base).resolve()
    out = Path(args.output)
    csv_path = Path(args.csv).resolve()
    residual = Path(args.residual).resolve() if args.residual else None

    lines = base.read_text(encoding="utf-8").splitlines(keepends=True)
    replace_or_insert(lines, "max_frames", str(args.frames))
    replace_or_insert(lines, "enable_measurement_csv", "1")
    replace_or_insert(lines, "measurement_csv_path", f'"{csv_path}"')
    replace_or_insert(lines, "standard_true_mm", str(args.true_mm))
    replace_or_insert(
        lines,
        "residual_compensation_file",
        f'"{residual}"' if residual else '""',
    )
    replace_or_insert(lines, "show_grid_guide", "0")
    replace_or_insert(lines, "grid_guide_position", '""')
    replace_or_insert(lines, "show_window", "1")

    # 临时配置位于 /tmp，正式配置里的相对标定目录需要转为项目内绝对路径。
    project_root = base.parent.parent
    camera_dir = project_root / "camera"
    if camera_dir.exists():
        replace_or_insert(lines, "calibration_file", f'"{camera_dir}"')

    out.write_text("".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main()
