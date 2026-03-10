#!/usr/bin/env bash
set -e

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
MVS_ENV_SH="/opt/MVS/bin/set_env_path.sh"
APP_BIN="$BUILD_DIR/metal_metrology"
CONFIG_PATH="$ROOT_DIR/config/system.yaml"
REBUILD=0

while [[ $# -gt 0 ]]; do
	case "$1" in
		--rebuild)
			REBUILD=1
			shift
			;;
		--config)
			CONFIG_PATH="$2"
			shift 2
			;;
		--config=*)
			CONFIG_PATH="${1#*=}"
			shift
			;;
		*)
			echo "[ERROR] 未知参数: $1"
			echo "用法: bash scripts/run_demo.sh [--rebuild] [--config PATH]"
			exit 1
			;;
	esac
done

if [[ -f "$MVS_ENV_SH" ]]; then
	# shellcheck disable=SC1090
	source "$MVS_ENV_SH"
	echo "[*] 已加载 MVS 环境: $MVS_ENV_SH"
else
	echo "[WARN] 未找到 $MVS_ENV_SH，若使用 mvs:0 可能无法打开相机"
fi

if [[ -z "${OpenCV_DIR:-}" ]]; then
	if [[ -f "$HOME/projects/opencv_build/opencv-4.9.0/build/OpenCVConfig.cmake" ]]; then
		OpenCV_DIR="$HOME/projects/opencv_build/opencv-4.9.0/build"
	elif [[ -f "$HOME/projects/opencv_build/opencv-4.9.0/build/unix-install/OpenCVConfig.cmake" ]]; then
		OpenCV_DIR="$HOME/projects/opencv_build/opencv-4.9.0/build/unix-install"
	fi
fi

if [[ $REBUILD -eq 1 || ! -x "$APP_BIN" ]]; then
	if [[ -z "${OpenCV_DIR:-}" ]]; then
		echo "[ERROR] 未检测到 OpenCVConfig.cmake，请先设置 OpenCV_DIR"
		echo "例如: export OpenCV_DIR=/home/hzwang/projects/opencv_build/opencv-4.9.0/build"
		exit 1
	fi

	echo "[*] 开始重新编译（USE_ONNXRUNTIME=ON）..."
	cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DUSE_ONNXRUNTIME=ON -DOpenCV_DIR="$OpenCV_DIR"
	cmake --build "$BUILD_DIR" -j
else
	echo "[*] 快速模式：跳过编译，直接运行"
fi

"$APP_BIN" "$CONFIG_PATH"
