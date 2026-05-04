#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
CAPTURE_SCRIPT="$ROOT_DIR/tools/grid_capture/run_grid_capture.sh"
ANALYZE_SCRIPT="$ROOT_DIR/tools/grid_analysis/analyze_grid_csv.py"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
DEFAULT_OUTPUT="$ROOT_DIR/measurements/grid_raw_${TIMESTAMP}.csv"

POSITIONS=(LT CT RT LC C RC LB CB RB)
TRUE_MM=""
OK_FRAMES=50
MIN_OK_FRAMES=30
FRAMES=1200
MAX_ROUNDS=10
CONFIG="$ROOT_DIR/config/system.yaml"
OUTPUT="$DEFAULT_OUTPUT"
SUMMARY=""
RESIDUAL=""
REBUILD=0
PREVIEW=1
STRICT=0

usage() {
    cat <<'USAGE'
用法:
  bash tools/grid_capture/run_grid_batch.sh --true-mm 33.00 [--ok-frames 50]

参数:
  --true-mm VALUE       标准件真实尺寸，单位 mm
  --ok-frames N         每个位置目标 OK 有效帧数，默认 50
  --min-ok-frames N     每个位置最低可接受 OK 有效帧数，默认 30
  --frames N            单轮最大尝试帧数，默认 1200
  --max-rounds N        每个位置最多采集轮数，默认 10
  --positions "C RT"    指定位置序列，默认 LT CT RT LC C RC LB CB RB
  --config PATH         基础配置，默认 config/system.yaml
  --output PATH         原始汇总 CSV，默认 measurements/grid_raw_时间.csv
  --summary PATH        统计 CSV，默认与 output 同名派生
  --residual PATH       残差 CSV，默认与 output 同名派生
  --rebuild             首个位置采集前强制重编译
  --no-preview          跳过每个位置的实时预览
  --strict              某个位置未达到目标 OK 帧数时中断批量流程
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --true-mm)
            TRUE_MM="$2"
            shift 2
            ;;
        --true-mm=*)
            TRUE_MM="${1#*=}"
            shift
            ;;
        --ok-frames)
            OK_FRAMES="$2"
            shift 2
            ;;
        --ok-frames=*)
            OK_FRAMES="${1#*=}"
            shift
            ;;
        --min-ok-frames)
            MIN_OK_FRAMES="$2"
            shift 2
            ;;
        --min-ok-frames=*)
            MIN_OK_FRAMES="${1#*=}"
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
        --max-rounds)
            MAX_ROUNDS="$2"
            shift 2
            ;;
        --max-rounds=*)
            MAX_ROUNDS="${1#*=}"
            shift
            ;;
        --positions)
            read -r -a POSITIONS <<< "$2"
            shift 2
            ;;
        --positions=*)
            read -r -a POSITIONS <<< "${1#*=}"
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
        --summary)
            SUMMARY="$2"
            shift 2
            ;;
        --summary=*)
            SUMMARY="${1#*=}"
            shift
            ;;
        --residual)
            RESIDUAL="$2"
            shift 2
            ;;
        --residual=*)
            RESIDUAL="${1#*=}"
            shift
            ;;
        --rebuild)
            REBUILD=1
            shift
            ;;
        --no-preview)
            PREVIEW=0
            shift
            ;;
        --strict)
            STRICT=1
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

if [[ -z "$TRUE_MM" ]]; then
    echo "[ERROR] 必须提供 --true-mm" >&2
    exit 1
fi
for numeric_arg in "$FRAMES" "$OK_FRAMES" "$MIN_OK_FRAMES" "$MAX_ROUNDS"; do
    if [[ ! "$numeric_arg" =~ ^[0-9]+$ || "$numeric_arg" -le 0 ]]; then
        echo "[ERROR] --frames/--ok-frames/--min-ok-frames/--max-rounds 必须是正整数" >&2
        exit 1
    fi
done
if (( MIN_OK_FRAMES > OK_FRAMES )); then
    echo "[ERROR] --min-ok-frames 不能大于 --ok-frames" >&2
    exit 1
fi

for pos in "${POSITIONS[@]}"; do
    case "$pos" in
        LT|CT|RT|LC|C|RC|LB|CB|RB) ;;
        *)
            echo "[ERROR] 非法位置: $pos" >&2
            exit 1
            ;;
    esac
done

case "$(realpath -m "$OUTPUT")" in
    "$ROOT_DIR"/measurements/*|/tmp/*) ;;
    *)
        echo "[ERROR] --output 只能写入 measurements/ 或 /tmp: $OUTPUT" >&2
        exit 1
        ;;
esac

output_stem="${OUTPUT%.*}"
if [[ -z "$SUMMARY" ]]; then
    if [[ "$output_stem" == *grid_raw* ]]; then
        SUMMARY="${output_stem/grid_raw/grid_summary}.csv"
    else
        SUMMARY="${output_stem}_summary.csv"
    fi
fi
if [[ -z "$RESIDUAL" ]]; then
    if [[ "$output_stem" == *grid_raw* ]]; then
        RESIDUAL="${output_stem/grid_raw/residual_grid}.csv"
    else
        RESIDUAL="${output_stem}_residual.csv"
    fi
fi

for path in "$SUMMARY" "$RESIDUAL"; do
    case "$(realpath -m "$path")" in
        "$ROOT_DIR"/measurements/*|/tmp/*) ;;
        *)
            echo "[ERROR] summary/residual 只能写入 measurements/ 或 /tmp: $path" >&2
            exit 1
            ;;
    esac
done

mkdir -p "$(dirname "$OUTPUT")"
rm -f "$OUTPUT" "$SUMMARY" "$RESIDUAL"

echo "[GRID] 批量九宫格采集开始"
echo "[GRID] 位置序列: ${POSITIONS[*]}"
echo "[GRID] 总位置数: ${#POSITIONS[@]}"
echo "[GRID] 每位置目标 OK 有效帧: $OK_FRAMES"
echo "[GRID] 每位置最低可接受 OK 有效帧: $MIN_OK_FRAMES"
echo "[GRID] 输出: $OUTPUT"

rebuild_args=()
if [[ $REBUILD -eq 1 ]]; then
    rebuild_args=(--rebuild)
fi
preview_args=()
if [[ $PREVIEW -eq 0 ]]; then
    preview_args=(--no-preview)
fi

position_status=()

ok_count_for() {
    python3 "$ROOT_DIR/tools/grid_capture/grid_csv_status.py" \
        --input "$OUTPUT" \
        --position "$1" \
        --quality OK \
        --field count
}

summary_for() {
    python3 "$ROOT_DIR/tools/grid_capture/grid_csv_status.py" \
        --input "$OUTPUT" \
        --position "$1" \
        --quality OK
}

for idx in "${!POSITIONS[@]}"; do
    pos="${POSITIONS[$idx]}"
    human_idx=$((idx + 1))
    echo
    echo "[GRID] ========================================"
    echo "[GRID] 位置 ${human_idx}/${#POSITIONS[@]}: $pos"
    read -r -p "[GRID] 按回车进入 ${pos} 位置预览/采集..."

    capture_rc=0
    "$CAPTURE_SCRIPT" \
        --position "$pos" \
        --true-mm "$TRUE_MM" \
        --ok-frames "$OK_FRAMES" \
        --min-ok-frames "$MIN_OK_FRAMES" \
        --frames "$FRAMES" \
        --max-rounds "$MAX_ROUNDS" \
        --config "$CONFIG" \
        --output "$OUTPUT" \
        --preview-now \
        --continue-on-short \
        "${rebuild_args[@]}" \
        "${preview_args[@]}" || capture_rc=$?

    rebuild_args=()
    count="$(ok_count_for "$pos")"
    summary="$(summary_for "$pos")"
    if (( count >= OK_FRAMES )); then
        status="OK"
    elif (( count >= MIN_OK_FRAMES )); then
        status="WARN"
        echo "[GRID] ${pos} 数据不足但可用于初步评估，继续下一个位置: ${summary}"
    else
        status="FAIL"
        echo "[GRID] ${pos} 有效帧不足，继续下一个位置并在最终报告中标记: ${summary}" >&2
    fi
    position_status+=("${pos},${status},${count},${summary}")

    if [[ $STRICT -eq 1 && ( "$status" != "OK" || $capture_rc -ne 0 ) ]]; then
        echo "[ERROR] strict 模式: ${pos} 未达到 ${OK_FRAMES} 条 OK，停止批量采集" >&2
        break
    fi
    if (( human_idx < ${#POSITIONS[@]} )); then
        next_pos="${POSITIONS[$human_idx]}"
        echo "[GRID] ${pos} 完成，准备进入下一个位置 ${next_pos}"
    fi
done

if [[ -f "$OUTPUT" ]]; then
    python3 "$ANALYZE_SCRIPT" \
        --input "$OUTPUT" \
        --summary "$SUMMARY" \
        --residual "$RESIDUAL"
else
    echo "[ERROR] 没有任何 OK 有效帧，无法生成补偿参数文件: $OUTPUT" >&2
    exit 1
fi

echo "[GRID] 九宫格采集完成"
echo "[GRID] raw: $OUTPUT"
echo "[GRID] summary: $SUMMARY"
echo "[GRID] residual: $RESIDUAL"
echo "[GRID] 补偿参数文件: $RESIDUAL"
echo "[GRID] 位置状态:"
for item in "${position_status[@]}"; do
    IFS=',' read -r pos status count summary <<< "$item"
    echo "[GRID] ${pos}: ${status}, OK=${count}/${OK_FRAMES}, ${summary}"
done
for item in "${position_status[@]}"; do
    IFS=',' read -r _ status _ _ <<< "$item"
    if [[ "$status" == "FAIL" ]]; then
        echo "[WARN] 存在低于 ${MIN_OK_FRAMES} 条 OK 的位置，补偿表仅供诊断，不建议直接启用。" >&2
        break
    fi
done
