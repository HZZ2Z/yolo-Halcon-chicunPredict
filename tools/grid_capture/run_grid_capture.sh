#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BASE_CONFIG="$ROOT_DIR/config/system.yaml"
RUN_SCRIPT="$ROOT_DIR/scripts/run_demo.sh"
APP_BIN="$ROOT_DIR/build/metal_metrology"
OUT_DIR="$ROOT_DIR/measurements"
RAW_CSV="$OUT_DIR/grid_raw.csv"
TMP_CONFIG="/tmp/hik_grid_capture.yaml"
PREVIEW_CONFIG="/tmp/hik_grid_preview.yaml"

POSITION=""
TRUE_MM=""
FRAMES=1200
OK_FRAMES=50
MIN_OK_FRAMES=30
MAX_ROUNDS=10
CONFIG="$BASE_CONFIG"
OUTPUT="$RAW_CSV"
PREVIEW=1
PREVIEW_PROMPT=1
REBUILD=0
INCLUDE_QUALITY="OK"
CONTINUE_ON_SHORT=0

usage() {
    cat <<'USAGE'
用法:
  bash tools/grid_capture/run_grid_capture.sh --position C --true-mm 33.00 [--ok-frames 50]

注意:
  这是单位置采集工具，只会采集 --position 指定的一个位置。
  完整九宫格请使用:
  bash tools/grid_capture/run_grid.sh --true-mm 33.00 --ok-frames 50 --min-ok-frames 30 --rebuild

参数:
  --position POS    九宫格位置: LT CT RT LC C RC LB CB RB
  --true-mm VALUE   标准件真实尺寸，单位 mm
  --ok-frames N     目标 OK 有效帧数，默认 50
  --min-ok-frames N 最低可接受 OK 有效帧数，默认 30
  --frames N        单轮最大尝试帧数，默认 1200
  --max-rounds N    单位置最多采集轮数，默认 10
  --config PATH     基础配置，默认 config/system.yaml
  --output PATH     汇总 CSV，默认 measurements/grid_raw.csv
  --include-quality all
                  排查时保留低质量帧；默认只保存 quality=OK
  --rebuild         采集前强制重新编译主程序
  --no-preview      跳过九宫格实时预览，直接开始采集
  --preview-now     直接启动预览，不再额外等待回车
  --continue-on-short
                  未达到目标但达到最低有效帧时返回成功，供批量采集继续下一位置

说明:
  工具只生成 /tmp 临时配置，不修改正式 config/system.yaml。
  主程序单次采集先写入 /tmp，再由工具追加九宫格位置列到汇总 CSV。
  默认先显示九宫格预览画面；摆放确认后按 q 或 Esc 退出预览。
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --position)
            POSITION="$2"
            shift 2
            ;;
        --position=*)
            POSITION="${1#*=}"
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
        --max-rounds)
            MAX_ROUNDS="$2"
            shift 2
            ;;
        --max-rounds=*)
            MAX_ROUNDS="${1#*=}"
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
        --include-quality)
            INCLUDE_QUALITY="$2"
            shift 2
            ;;
        --include-quality=*)
            INCLUDE_QUALITY="${1#*=}"
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
        --preview-now)
            PREVIEW_PROMPT=0
            shift
            ;;
        --continue-on-short)
            CONTINUE_ON_SHORT=1
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

case "$POSITION" in
    LT|CT|RT|LC|C|RC|LB|CB|RB) ;;
    *)
        echo "[ERROR] --position 必须是 LT CT RT LC C RC LB CB RB 之一" >&2
        exit 1
        ;;
esac

if [[ -z "$TRUE_MM" ]]; then
    echo "[ERROR] 必须提供 --true-mm" >&2
    exit 1
fi

if [[ "$INCLUDE_QUALITY" != "OK" && "$INCLUDE_QUALITY" != "all" ]]; then
    echo "[ERROR] --include-quality 只能是 OK 或 all" >&2
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

if [[ ! -f "$CONFIG" ]]; then
    echo "[ERROR] 配置文件不存在: $CONFIG" >&2
    exit 1
fi

echo "[GRID] 注意: 当前是单位置采集工具，只会采集 ${POSITION}。"
echo "[GRID] 完整九宫格请使用: bash tools/grid_capture/run_grid.sh --true-mm ${TRUE_MM} --ok-frames ${OK_FRAMES} --min-ok-frames ${MIN_OK_FRAMES}"

case "$(realpath -m "$OUTPUT")" in
    "$ROOT_DIR"/measurements/*|/tmp/*) ;;
    *)
        echo "[ERROR] --output 只能写入 measurements/ 或 /tmp: $OUTPUT" >&2
        exit 1
        ;;
esac

mkdir -p "$(dirname "$OUTPUT")"

needs_rebuild() {
    if [[ $REBUILD -eq 1 || ! -x "$APP_BIN" ]]; then
        return 0
    fi
    if [[ "$ROOT_DIR/CMakeLists.txt" -nt "$APP_BIN" ]]; then
        return 0
    fi
    if [[ -n "$(find "$ROOT_DIR/src" "$ROOT_DIR/include" -type f \
        \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) \
        -newer "$APP_BIN" -print -quit 2>/dev/null)" ]]; then
        return 0
    fi
    return 1
}

RUN_ARGS=()
if needs_rebuild; then
    RUN_ARGS+=(--rebuild)
    echo "[GRID] 检测到二进制不存在、过旧或用户要求重编译，将先执行 --rebuild"
fi

ok_count() {
    python3 "$ROOT_DIR/tools/grid_capture/grid_csv_status.py" \
        --input "$OUTPUT" \
        --position "$POSITION" \
        --quality OK \
        --field count
}

print_position_status() {
    python3 "$ROOT_DIR/tools/grid_capture/grid_csv_status.py" \
        --input "$OUTPUT" \
        --position "$POSITION" \
        --quality OK
}

if [[ $PREVIEW -eq 1 ]]; then
    python3 "$ROOT_DIR/tools/grid_capture/write_temp_config.py" \
        --base "$CONFIG" \
        --output "$PREVIEW_CONFIG" \
        --csv "/tmp/hik_grid_preview.csv" \
        --true-mm "$TRUE_MM" \
        --frames -1 \
        --position "$POSITION" \
        --disable-csv \
        --preview

    echo "[GRID] 预览位置: $POSITION"
    echo "[GRID] 请根据画面中的九宫格高亮区域摆放标准件"
    echo "[GRID] 摆放确认后，在图像窗口按 q 或 Esc 退出预览"
    if [[ $PREVIEW_PROMPT -eq 1 ]]; then
        read -r -p "[GRID] 按回车启动实时预览..."
    fi
    "$RUN_SCRIPT" "${RUN_ARGS[@]}" --config "$PREVIEW_CONFIG"
    RUN_ARGS=()
fi

echo "[GRID] 位置: $POSITION"
echo "[GRID] 真实尺寸: $TRUE_MM mm"
echo "[GRID] 目标有效帧: $OK_FRAMES"
echo "[GRID] 最低可接受有效帧: $MIN_OK_FRAMES"
echo "[GRID] 单轮最大尝试帧数: $FRAMES"
echo "[GRID] 汇总输出: $OUTPUT"
echo "[GRID] 运行后画面会显示九宫格引导和当前位置 ${POSITION}"
read -r -p "[GRID] 请把标准件放到 ${POSITION} 位置，按回车开始采集..."

for ((round = 1; round <= MAX_ROUNDS; ++round)); do
    current_ok="$(ok_count)"
    if (( current_ok >= OK_FRAMES )); then
        echo "[GRID] ${POSITION} 已满足目标: $(print_position_status)"
        exit 0
    fi

    session_csv="/tmp/hik_grid_capture_session_${POSITION}_${round}.csv"
    rm -f "$session_csv"

    python3 "$ROOT_DIR/tools/grid_capture/write_temp_config.py" \
        --base "$CONFIG" \
        --output "$TMP_CONFIG" \
        --csv "$session_csv" \
        --true-mm "$TRUE_MM" \
        --frames "$FRAMES" \
        --position "$POSITION"

    echo "[GRID] ${POSITION} 第 ${round}/${MAX_ROUNDS} 轮采集，当前 OK=${current_ok}/${OK_FRAMES}"
    "$RUN_SCRIPT" "${RUN_ARGS[@]}" --config "$TMP_CONFIG"
    RUN_ARGS=()

    remaining=$((OK_FRAMES - current_ok))
    python3 "$ROOT_DIR/tools/grid_capture/tag_grid_csv.py" \
        --input "$session_csv" \
        --output "$OUTPUT" \
        --position "$POSITION" \
        --include-quality "$INCLUDE_QUALITY" \
        --limit "$remaining"

    current_ok="$(ok_count)"
    echo "[GRID] ${POSITION} 当前统计: $(print_position_status)"
    if (( current_ok >= OK_FRAMES )); then
        echo "[GRID] ${POSITION} 采集完成"
        exit 0
    fi
done

final_ok="$(ok_count)"
final_summary="$(print_position_status)"
if (( final_ok >= MIN_OK_FRAMES )); then
    echo "[GRID] ${POSITION} 数据不足但可用于初步评估: OK=${final_ok}/${OK_FRAMES}, min=${MIN_OK_FRAMES}"
    echo "[GRID] 当前统计: ${final_summary}"
    if [[ $CONTINUE_ON_SHORT -eq 1 ]]; then
        exit 0
    fi
fi

echo "[ERROR] ${POSITION} 达到最大轮数仍未获得 ${OK_FRAMES} 条 OK 有效帧" >&2
echo "[ERROR] 当前统计: ${final_summary}" >&2
exit 1
