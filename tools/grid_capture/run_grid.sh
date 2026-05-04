#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BATCH_SCRIPT="$ROOT_DIR/tools/grid_capture/run_grid_batch.sh"
SINGLE_SCRIPT="$ROOT_DIR/tools/grid_capture/run_grid_capture.sh"

SINGLE_POSITION=""
PASSTHROUGH=()

usage() {
    cat <<'USAGE'
用法:
  bash tools/grid_capture/run_grid.sh --true-mm 33.00 --ok-frames 50 --min-ok-frames 30 --rebuild

说明:
  默认执行完整九宫格批量采集，顺序为 LT CT RT LC C RC LB CB RB。
  采集完成后会自动生成 grid_summary 和 residual_grid 补偿参数文件。

常用参数:
  --true-mm VALUE       标准件真实尺寸，单位 mm
  --ok-frames N         每个位置目标 OK 有效帧数，默认 50
  --min-ok-frames N     每个位置最低可接受 OK 有效帧数，默认 30
  --positions "C RT"    调试时指定位置序列
  --rebuild             首次运行前强制重编译
  --no-preview          跳过每个位置的实时预览

高级调试:
  --single-position POS 只采一个位置；例如 --single-position C

更多批量参数会原样转发给 run_grid_batch.sh。
更多单点参数会原样转发给 run_grid_capture.sh。
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --single-position)
            SINGLE_POSITION="$2"
            shift 2
            ;;
        --single-position=*)
            SINGLE_POSITION="${1#*=}"
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            PASSTHROUGH+=("$1")
            shift
            ;;
    esac
done

if [[ -n "$SINGLE_POSITION" ]]; then
    echo "[GRID] 高级单点模式: 只采集 ${SINGLE_POSITION}"
    echo "[GRID] 如需完整九宫格，请不要传 --single-position。"
    exec "$SINGLE_SCRIPT" --position "$SINGLE_POSITION" "${PASSTHROUGH[@]}"
fi

echo "[GRID] 推荐入口: 完整九宫格批量采集"
exec "$BATCH_SCRIPT" "${PASSTHROUGH[@]}"
