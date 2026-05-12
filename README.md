# AAA工业金属零件视觉测量系统

本项目是生产交付版视觉测量系统，面向 **Hikrobot 工业相机 + YOLO OBB ONNX + HALCON 标定 + 多扫描线亚像素测量** 的在线尺寸检测场景。

当前版本保留命令行测量程序，并新增本机 Qt 桌面端包装。系统精度优化来自 HALCON 标定点级世界映射、数值雅可比误差传播、多扫描线卡尺和鲁棒聚合。

## 快速运行

命令行测量入口：

首次运行或清理构建目录后：

```bash
bash scripts/run_demo.sh --rebuild
```

已有可执行文件时：

```bash
bash scripts/run_demo.sh
```

指定配置文件：

```bash
bash scripts/run_demo.sh --config config/system.yaml
```

桌面端入口：

```bash
bash scripts/run_desktop.sh --rebuild
```

桌面端首次启动会创建本地管理员账号，之后通过账号密码登录。界面采用工业科技风布局，登录页使用 Qt 本地绘制动态背景，实时页使用数据卡片展示 `px/mm/sigma/scans`。测量记录保存在 `data/metrology.db`，历史页按“一次开始/停止测量”为一条运行记录，双击记录可查看该次运行的逐帧明细。

安装桌面图标后可双击启动：

```bash
bash scripts/install_desktop_launcher.sh
```

该命令会在桌面和应用菜单创建 `Metal Metrology` 入口；如果尚未构建桌面端，先运行 `bash scripts/run_desktop.sh --rebuild`。

## 生产主体

- 构建入口：`CMakeLists.txt`
- 程序源码：`src/`
- 公共头文件：`include/`
- 运行配置：`config/system.yaml`、`config/classes.txt`
- 标定文件：`camera/相机内参.cal`、`camera/相机位姿.dat`
- 模型文件：`models/best_opset12.onnx`
- 运行脚本：`scripts/run_demo.sh`
- 桌面端脚本：`scripts/run_desktop.sh`
- 桌面启动器安装脚本：`scripts/install_desktop_launcher.sh`
- 相机排障脚本：`scripts/check_camera.sh`
- 文档：`运行说明.md`、`docs/architecture.md`、`docs/system_analysis_report.md`

外部依赖需要在目标机器安装：

- OpenCV
- HALCON
- Hikrobot MVS SDK
- ONNXRuntime GPU 版本
- CUDA / cuDNN
- Qt Widgets / Qt Sql 开发包（仅桌面端需要）

## 桌面端功能

- 登录：本地 SQLite 账号，支持 `admin` 与 `operator` 两类角色。
- 实时测量：显示相机画面、OBB、边缘点、`px/mm/sigma/scans` 与低质量提示。
- 数据存储：一次运行保存为一条批次记录，批次下保存逐帧测量明细，包含时间、用户、像素值、毫米值、sigma、扫描线数量、质量状态、目标位置和角度。
- 历史查询：按用户和运行状态筛选批次记录，双击批次可查看帧明细，支持删除选中、批量删除和一键清空。
- 导出：选中批次时导出该批次帧明细；未选中批次时导出批次汇总，不恢复自动生产 CSV 采集链路。
- 设置：第一版只开放相机源、最大帧数和显示尺寸等非算法核心参数。

## 系统流程

1. `main.cpp` 读取 `config/system.yaml`。
2. `CalibrationMapper` 加载 HALCON `.cal/.dat`，并通过点级 `pixelToWorld()` 建立毫米映射。
3. `CameraProvider` 打开 Hikrobot MVS 相机。
4. `OnnxObbInferencer` 使用 ONNXRuntime CUDA EP 执行 YOLO OBB 推理。
5. `ObbTracker` 使用 EKF 平滑 OBB 中心和角度。
6. `SubpixelCaliper` 围绕 OBB 生成多条平行扫描线，提取亚像素边缘。
7. `measurement_uncertainty` 将像素边缘协方差传播到世界平面，输出毫米不确定度。
8. 帧处理层按 `sigma/scans/跳变` 判断测量质量，坏帧不进入平滑结果。
9. 命令行入口使用 OpenCV 窗口显示结果；桌面入口通过回调把结果传给 Qt UI。

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
- `max_sigma_mm / min_valid_scan_count / max_frame_jump_mm`：测量质量控制阈值。
- `min_edge_length_ratio / fallback_to_abs_gradient`：外边界合理性保护；极性感知测量明显偏短时回退到绝对梯度搜索。
- `display_max_width / display_max_height`：显示窗口最大尺寸；图像保持比例缩放，避免 Ubuntu 小屏幕裁掉画面。

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

## 常见问题

### 相机打不开

先执行：

```bash
bash scripts/check_camera.sh
```

确认相机连接、供电、MVS SDK 环境和权限。

### GPU 推理未启用

当前生产配置要求 ONNXRuntime CUDA EP 可用。若 CUDA EP 启用失败，程序会直接退出，避免误用 CPU 推理影响实时性。

### 没有毫米值

确认 `strict_calibration: 1`，并检查 `camera/相机内参.cal` 与 `camera/相机位姿.dat` 是否来自当前现场安装姿态。

如果日志出现 `HALCON error #2042: Feature has expired`，说明 HALCON 授权功能过期，需要先恢复 HALCON license，重新编译不能解决该问题。

本仓库只包含生产推理和测量链路，不包含模型训练代码、单元测试代码和开发调试构建产物。
