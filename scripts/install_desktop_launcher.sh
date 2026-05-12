#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
APP_BIN="$ROOT_DIR/build/metal_metrology_desktop"
RUN_SCRIPT="$ROOT_DIR/scripts/run_desktop.sh"
ICON_PATH="$ROOT_DIR/assets/metal_metrology.svg"
APP_ID="metal-metrology"
APP_NAME="Metal Metrology"
REBUILD=0

usage() {
	cat <<EOF
用法: bash scripts/install_desktop_launcher.sh [--rebuild]

功能:
  为当前用户安装桌面图标和应用菜单入口，双击启动 Qt 桌面端。

选项:
  --rebuild    安装前先执行 bash scripts/run_desktop.sh --rebuild
EOF
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--rebuild)
			REBUILD=1
			shift
			;;
		--help|-h)
			usage
			exit 0
			;;
		*)
			echo "[ERROR] 未知参数: $1"
			usage
			exit 1
			;;
	esac
done

if [[ ! -f "$RUN_SCRIPT" ]]; then
	echo "[ERROR] 未找到启动脚本: $RUN_SCRIPT"
	exit 1
fi

if [[ ! -f "$ICON_PATH" ]]; then
	echo "[ERROR] 未找到图标文件: $ICON_PATH"
	exit 1
fi

mkdir -p "$ROOT_DIR/data"

if [[ $REBUILD -eq 1 ]]; then
	echo "[*] 安装前重新构建桌面端..."
	if [[ -z "${OpenCV_DIR:-}" ]]; then
		if [[ -f "$HOME/projects/opencv_build/opencv-4.9.0/build/OpenCVConfig.cmake" ]]; then
			OpenCV_DIR="$HOME/projects/opencv_build/opencv-4.9.0/build"
		elif [[ -f "$HOME/projects/opencv_build/opencv-4.9.0/build/unix-install/OpenCVConfig.cmake" ]]; then
			OpenCV_DIR="$HOME/projects/opencv_build/opencv-4.9.0/build/unix-install"
		fi
	fi
	if [[ -z "${OpenCV_DIR:-}" ]]; then
		echo "[ERROR] 未检测到 OpenCVConfig.cmake，请先设置 OpenCV_DIR"
		exit 1
	fi
	cmake -S "$ROOT_DIR" -B "$ROOT_DIR/build" -DUSE_ONNXRUNTIME=ON -DBUILD_DESKTOP=ON -DOpenCV_DIR="$OpenCV_DIR"
	cmake --build "$ROOT_DIR/build" -j
fi

if [[ ! -x "$APP_BIN" ]]; then
	echo "[ERROR] 未找到桌面端可执行文件: $APP_BIN"
	echo "请先运行:"
	echo "  bash scripts/run_desktop.sh --rebuild"
	echo "然后再运行:"
	echo "  bash scripts/install_desktop_launcher.sh"
	exit 1
fi

desktop_dir="${XDG_DESKTOP_DIR:-}"
if [[ -z "$desktop_dir" && -f "$HOME/.config/user-dirs.dirs" ]]; then
	desktop_dir="$(grep '^XDG_DESKTOP_DIR=' "$HOME/.config/user-dirs.dirs" | cut -d= -f2- | tr -d '"')"
	desktop_dir="${desktop_dir/#\$HOME/$HOME}"
fi
if [[ -z "$desktop_dir" ]]; then
	if [[ -d "$HOME/桌面" ]]; then
		desktop_dir="$HOME/桌面"
	else
		desktop_dir="$HOME/Desktop"
	fi
fi

applications_dir="$HOME/.local/share/applications"
desktop_file="$desktop_dir/$APP_NAME.desktop"
menu_file="$applications_dir/$APP_ID.desktop"

mkdir -p "$desktop_dir" "$applications_dir"

write_desktop_file() {
	local target="$1"
	cat > "$target" <<EOF
[Desktop Entry]
Type=Application
Name=$APP_NAME
Name[zh_CN]=金属视觉测量系统
Comment=Industrial vision measurement desktop console
Comment[zh_CN]=工业视觉测量桌面端
Exec=bash "$RUN_SCRIPT"
Path=$ROOT_DIR
Icon=$ICON_PATH
Terminal=false
StartupNotify=true
Categories=Utility;Science;Engineering;
EOF
	chmod +x "$target"
}

write_desktop_file "$desktop_file"
write_desktop_file "$menu_file"

if command -v gio >/dev/null 2>&1; then
	gio set "$desktop_file" metadata::trusted true >/dev/null 2>&1 || true
fi

if command -v update-desktop-database >/dev/null 2>&1; then
	update-desktop-database "$applications_dir" >/dev/null 2>&1 || true
fi

echo "[OK] 已安装桌面启动器:"
echo "  $desktop_file"
echo "[OK] 已安装应用菜单入口:"
echo "  $menu_file"
echo
echo "现在可以在桌面双击 \"$APP_NAME\" 启动系统。"
