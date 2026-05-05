# 工业金属零件视觉测量系统

本项目是生产交付版推理与测量系统，面向  Hikrobot 工业相机 + YOLO OBB ONNX + HALCON 标定 + 多扫描线亚像素测量 的在线尺寸检测场景。

当前交付包只保留正式运行所需源码、配置、模型、标定文件、脚本和文档；测试用例、临时构建目录、调试日志和探针配置已移除。

## 快速运行

首次运行或清理构建目录后，执行：
bash scripts/run_demo.sh --rebuild


已有可执行文件时，可直接运行：
bash scripts/run_demo.sh

指定配置文件：
bash scripts/run_demo.sh --config config/system.yaml

## 软件架构

- 构建入口：`CMakeLists.txt`
- 程序源码：`src/`
- 公共头文件：`include/`
- 运行配置：`config/system.yaml`、`config/classes.txt`
- 标定文件：`camera/相机参数.cal`、`camera/相机位姿.dat`
- 模型文件：`models/best_opset12.onnx`
- 运行脚本：`scripts/run_demo.sh`
- 相机排障脚本：`scripts/check_camera.sh`
- 文档：`运行说明.md`、`docs/architecture.md`、`docs/system_analysis_report.md`

外部依赖需要在目标机器安装：

- OpenCV
- HALCON
- Hikrobot MVS SDK
- ONNXRuntime GPU 版本
- CUDA / cuDNN

## 系统流程

1. main.cpp读取config/system.yaml。
2. CalibrationMapper加载相机标定文件.cal/.dat，并通过pixelToWorld()建立毫米映射。
3. CameraProvider打开Hikrobot MVS相机。
4. OnnxObbInferencer使用ONNXRuntime CUDA EP 执行 YOLO OBB 推理。
5. ObbTracker 使用 EKF 平滑 OBB 中心和角度。
6. SubpixelCaliper 围绕 OBB 生成多条平行扫描线，使用极性感知边缘搜索和扫描线质量评分提取亚像素边缘。
7. measurement_uncertainty 将像素边缘协方差传播到世界平面，输出毫米不确定度。
8. 帧处理层按 sigma/scans/跳变 判断测量质量，坏帧不进入平滑结果。
9. FrameVisualizer 显示 OBB、测量点、`px`、`mm`、`sigma`、`scans` 和低质量告警。

## 关键配置

- `input_source: "mvs:0"`：使用第 0 台 Hikrobot MVS 相机。
- `onnx_model_path`：正式 ONNX 模型路径。
- `calibration_file`：HALCON 标定目录，默认指向 `camera`。
- `strict_calibration: 1`：生产模式要求毫米标定可用。
- `infer_interval / measure_interval / render_interval`：推理、测量、渲染分频。
- `caliper_length: -1.0` + `use_obb_adaptive_caliper: 1`：卡尺长度由 OBB 自适应决定。
- `multi_scan_count: 9`：默认使用 9 条平行扫描线。
- `edge_refine_half_window / edge_power_gamma`：亚像素边缘加权窗口和梯度权重。
- `huber_delta_mm`：世界平面鲁棒聚合阈值。
- `estimate_measurement_uncertainty: 1`：输出毫米标准差估计。
- `max_sigma_mm / min_valid_scan_count / max_frame_jump_mm`：测量质量控制阈值，低质量帧不会更新最终平滑值。
- `min_edge_length_ratio / fallback_to_abs_gradient`：外边界合理性保护；极性感知测量明显偏短时回退到传统绝对梯度搜索。
- `enable_measurement_csv / measurement_csv_path / standard_true_mm`：九宫格标准件实验的数据记录配置。
- `residual_compensation_file`：可选空间残差补偿表；为空时完全关闭。
- `display_max_width / display_max_height`：显示窗口最大尺寸；图像会保持比例缩放，避免 Ubuntu 小屏幕裁掉画面。

## 输出含义

窗口左上角会显示：

- `px`：当前平滑后的像素尺寸。
- `mm`：当前平滑后的毫米尺寸。
- `sigma`：毫米标准差估计，数值越小表示当前帧测量不确定度越低。
- `scans`：有效参与聚合的扫描线数量，正常情况下应接近 `multi_scan_count`。
- `LOW QUALITY`：当前帧被质量控制拒绝，当前边缘点仍会显示，但该帧不会进入最终平滑历史。

如果 HALCON 标定不可用且关闭 strict 模式，系统仍可显示像素尺寸，但毫米结果会显示为不可用。

## 标定与畸变处理

系统保留原图坐标系进行检测和显示，不做整帧 world-plane 重采样。

毫米测量在边缘点级完成：

1. 多扫描线提取左右边缘像素点。
2. 每个边缘点通过 HALCON `ImagePointsToWorldPlane` 映射到世界平面。
3. 在世界平面中使用共享法向和 Huber 权重聚合尺寸。
4. 使用数值雅可比把像素边缘协方差传播为 `sigma`。

HALCON `.cal/.dat` 读取时会复制到 `/tmp/hik_yoloobb_halcon_calib/` 下的 ASCII 文件名再加载，这是生产兼容逻辑，用于规避中文路径或中文文件名在 HALCON C++ wrapper 中的兼容风险。

初始化成功后会输出中心、边缘和角落的局部 `sx/sy mm/px` 摘要，用于检查全视场比例变化。该诊断不替代标准件验证，只帮助判断边缘区域误差更可能来自标定覆盖、测量平面还是边缘提取。

## 位置畸变误差补偿还没调试好，不建议使用。

正式启用残差补偿前，先用标准件做九宫格实验：

1. 固定零件角度，在 9 个视场位置分别采集 30-50 帧。
2. 使用 `tools/grid_capture/` 生成 `/tmp` 临时配置并采集 CSV。
3. 使用 `tools/grid_analysis/` 统计每个位置的 `raw_mm/mm/sigma/scans/error_mm`。
4. 若同位置稳定但不同位置均值存在稳定偏差，再生成残差补偿表。

补偿表进入主链路前，先用临时配置做受控测试：

```bash
bash tools/grid_analysis/run_compensation_test.sh \
  --residual measurements/residual_grid_YYYYmmdd_HHMMSS.csv \
  --true-mm 33.00 \
  --frames 1200
```

该命令只生成 `/tmp/hik_comp_test.yaml`，不会修改正式 `config/system.yaml`。测试 CSV 中的 `correction_mm` 非零时，说明主链路已经实际应用补偿；脚本会同时汇总补偿前后的均值、标准差和误差。

不要直接用少量截图或单帧结果生成残差补偿表。

`tools/` 是离线评估工具目录，不被 CMake 编译进主程序，也不会修改正式 `config/system.yaml`、`camera/` 或 `models/`。生产运行仍只使用 `bash scripts/run_demo.sh`。

## bug检查与调试

### 1.相机打不开

先执行：

```bash
bash scripts/check_camera.sh
```

确认相机连接、供电、MVS SDK 环境和权限。

### 2.GPU 推理未启用

当前生产配置要求 ONNXRuntime CUDA EP 可用。若 CUDA EP 启用失败，程序会直接退出，避免误用 CPU 推理影响实时性。

### 3.没有毫米值

确认 `strict_calibration: 1`，并检查 `camera/相机参数.cal` 与 `camera/相机位姿.dat` 是否来自当前现场安装姿态。

如果日志出现 `HALCON error #2042: Feature has expired`，说明 HALCON 授权功能过期，需要先恢复 HALCON license，重新编译不能解决该问题。

## 4.项目边界

本仓库只包含生产推理和测量链路，不包含模型训练代码、单元测试代码和开发调试构建产物。
