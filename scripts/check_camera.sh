#!/usr/bin/env bash
set -e

echo "[1/4] USB 设备中查找 Hikrobot..."
lsusb | grep -Ei 'hikrobot|hik|mv-ce' || echo "未在 lsusb 中匹配到 Hikrobot 关键字"

echo
echo "[2/4] 检查 V4L2 设备节点..."
if ls /dev/video* >/dev/null 2>&1; then
  ls -l /dev/video*
else
  echo "未发现 /dev/video*"
fi

echo
echo "[3/4] 列出 V4L2 设备..."
if command -v v4l2-ctl >/dev/null 2>&1; then
  v4l2-ctl --list-devices || true
else
  echo "未安装 v4l2-ctl（sudo apt install v4l-utils）"
fi

echo
echo "[4/4] 结论建议"
if ls /dev/video* >/dev/null 2>&1; then
  echo "检测到 V4L2 节点：可在 config/system.yaml 设置 input_source 为 \"0\" 或对应编号。"
else
  echo "未检测到 V4L2 节点：当前相机大概率不是 UVC 直出模式，OpenCV VideoCapture 无法直接采集。"
  echo "下一步应安装 Hikrobot MVS SDK，并用其示例程序验证取流；随后把本项目采集模块切换到 MVS 接口。"
fi
