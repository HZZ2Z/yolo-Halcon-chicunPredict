#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
RUN_SCRIPT="$ROOT_DIR/scripts/run_demo.sh"
WRITE_CONFIG="$ROOT_DIR/tools/grid_analysis/write_comp_test_config.py"
INSPECT_RESIDUAL="$ROOT_DIR/tools/grid_analysis/inspect_residual_grid.py"
SUMMARIZE_CSV="$ROOT_DIR/tools/grid_analysis/summarize_compensation_csv.py"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"

CONFIG="$ROOT_DIR/config/system.yaml"
MODE="compensated"
RESIDUAL=""
TRUE_MM="33.00"
FRAMES=1200
OUTPUT=""
TMP_CONFIG="/tmp/hik_comp_test.yaml"
REBUILD=0
RUN_APP=1

usage() {
    cat <<'USAGE'
用法:
  bash tools/grid_analysis/run_compensation_test.sh --residual measurements/residual_grid_20260504_162721.csv --true-mm 33.00

说明:
  该脚本只生成 /tmp 临时配置，不修改正式 config/system.yaml。
  compensated 模式会启用 residual_compensation_file 并写出补偿测试 CSV。
  baseline 模式会关闭补偿，用于同位置对照采集。

参数:
  --residual PATH       residual_grid 补偿表；compensated 模式默认取 measurements/ 下最新 residual_grid_*.csv
  --mode compensated    启用补偿测试，默认
  --mode baseline       关闭补偿，采集补偿前基线
  --true-mm VALUE       标准件真实尺寸，默认 33.00
  --frames N            最大采集帧数，默认 1200
  --config PATH         基础配置，默认 config/system.yaml
  --output PATH         输出 CSV，默认 measurements/comp_test_时间.csv 或 baseline_test_时间.csv
  --tmp-config PATH     临时配置路径，默认 /tmp/hik_comp_test.yaml
  --rebuild             运行前重新编译
  --no-run              只生成临时配置并打印运行命令
USAGE
}

latest_residual() {
    ls -t "$ROOT_DIR"/measurements/residual_grid_*.csv 2>/dev/null | head -n 1 || true
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --residual)
            RESIDUAL="$2"
            shift 2
            ;;
        --residual=*)
            RESIDUAL="${1#*=}"
            shift
            ;;
        --mode)
            MODE="$2"
            shift 2
            ;;
        --mode=*)
            MODE="${1#*=}"
            shift
            ;;
        --true-mm)
            TRUE_MM="$2"
            shift 2
            ;;
        --true-mm=*)
            TRUE_MM="${1#*=}"
            shift
            ;;
        --frames)
            FRAMES="$2"
            shift 2
            ;;
        --frames=*)
            FRAMES="${1#*=}"
            shift
            ;;
        --config)
            CONFIG="$2"
            shift 2
            ;;
        --config=*)
            CONFIG="${1#*=}"
            shift
            ;;
        --output)
            OUTPUT="$2"
            shift 2
            ;;
        --output=*)
            OUTPUT="${1#*=}"
            shift
            ;;
        --tmp-config)
            TMP_CONFIG="$2"
            shift 2
            ;;
        --tmp-config=*)
            TMP_CONFIG="${1#*=}"
            shift
            ;;
        --rebuild)
            REBUILD=1
            shift
            ;;
        --no-run)
            RUN_APP=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "[ERROR] 未知参数: $1" >&2
            usage
            exit 1
            ;;
    esac
done

case "$MODE" in
    compensated|baseline) ;;
    *)
        echo "[ERROR] --mode 只能是 compensated 或 baseline" >&2
        exit 1
        ;;
esac

if [[ ! "$FRAMES" =~ ^[0-9]+$ || "$FRAMES" -le 0 ]]; then
    echo "[ERROR] --frames 必须是正整数" >&2
    exit 1
fi
if [[ ! -f "$CONFIG" ]]; then
    echo "[ERROR] 配置文件不存在: $CONFIG" >&2
    exit 1
fi

if [[ "$MODE" == "compensated" ]]; then
    if [[ -z "$RESIDUAL" ]]; then
        RESIDUAL="$(latest_residual)"
    fi
    if [[ -z "$RESIDUAL" || ! -f "$RESIDUAL" ]]; then
        echo "[ERROR] 未找到 residual_grid 补偿表，请传 --residual PATH" >&2
        exit 1
    fi
    DEFAULT_OUTPUT="$ROOT_DIR/measurements/comp_test_${TIMESTAMP}.csv"
else
    RESIDUAL=""
    DEFAULT_OUTPUT="$ROOT_DIR/measurements/baseline_test_${TIMESTAMP}.csv"
fi
if [[ -z "$OUTPUT" ]]; then
    OUTPUT="$DEFAULT_OUTPUT"
fi

case "$(realpath -m "$OUTPUT")" in
    "$ROOT_DIR"/measurements/*|/tmp/*) ;;
    *)
        echo "[ERROR] --output 只能写入 measurements/ 或 /tmp: $OUTPUT" >&2
        exit 1
        ;;
esac
case "$(realpath -m "$TMP_CONFIG")" in
    /tmp/*) ;;
    *)
        echo "[ERROR] --tmp-config 只能写入 /tmp: $TMP_CONFIG" >&2
        exit 1
        ;;
esac

mkdir -p "$(dirname "$OUTPUT")"
rm -f "$OUTPUT"

echo "[COMP_TEST] mode: $MODE"
echo "[COMP_TEST] true_mm: $TRUE_MM"
echo "[COMP_TEST] frames: $FRAMES"
echo "[COMP_TEST] output: $OUTPUT"
if [[ "$MODE" == "compensated" ]]; then
    python3 "$INSPECT_RESIDUAL" --input "$RESIDUAL"
fi

python3 "$WRITE_CONFIG" \
    --base "$CONFIG" \
    --output "$TMP_CONFIG" \
    --csv "$OUTPUT" \
    --true-mm "$TRUE_MM" \
    --frames "$FRAMES" \
    --residual "$RESIDUAL"

echo "[COMP_TEST] temp_config: $TMP_CONFIG"
run_args=()
if [[ $REBUILD -eq 1 ]]; then
    run_args+=(--rebuild)
fi
echo "[COMP_TEST] run: bash scripts/run_demo.sh ${run_args[*]} --config $TMP_CONFIG"

if [[ $RUN_APP -eq 0 ]]; then
    exit 0
fi

"$RUN_SCRIPT" "${run_args[@]}" --config "$TMP_CONFIG"

if [[ -f "$OUTPUT" ]]; then
    python3 "$SUMMARIZE_CSV" --input "$OUTPUT" --true-mm "$TRUE_MM"
else
    echo "[WARN] 没有生成测试 CSV: $OUTPUT" >&2
fi
