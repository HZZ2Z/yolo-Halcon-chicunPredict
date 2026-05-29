# 工业金属零件视觉测量系统

本项目是一套面向工业金属零件尺寸检测的视觉测量系统，主要用于完成工业相机采集、旋转目标定位、亚像素边缘提取、相机标定换算、测量结果显示、质量判断和历史数据记录等功能。

系统采用 **Hikrobot 工业相机 + YOLO-OBB ONNX 推理 + EKF 跟踪 + 多扫描线亚像素卡尺 + HALCON 点级世界平面映射 + Huber 鲁棒聚合 + Qt 桌面端** 的整体方案。项目重点在生产推理和尺寸测量链路，不包含模型训练代码。

---

## 1. 项目特点

本系统不是直接使用目标检测框作为最终尺寸结果，而是采用“粗定位 + 精测量”的方式：

1. 使用 YOLO-OBB 旋转目标检测模型获取零件位置和姿态。
2. 使用 EKF 对目标中心和角度进行平滑，减少检测框抖动。
3. 根据稳定后的 OBB 生成多条平行扫描线。
4. 在原始图像坐标系中进行亚像素边缘提取。
5. 将每条扫描线的左右边缘点通过 HALCON `ImagePointsToWorldPlane` 映射到世界平面。
6. 在世界坐标系中进行 Huber 鲁棒聚合，得到毫米尺寸。
7. 使用数值雅可比进行误差传播，输出 `sigma` 作为本帧测量不确定度估计。
8. 根据 `sigma / scans / frame jump` 进行质量门控，过滤低质量帧。
9. 通过 OpenCV 或 Qt 界面实时显示测量结果，并支持 SQLite 历史记录和 CSV 导出。

核心测量链路如下：

```text
工业相机采集
    ↓
YOLO-OBB 推理 + Rotated NMS
    ↓
EKF 平滑目标中心和角度
    ↓
多扫描线亚像素边缘提取
    ↓
HALCON pixelToWorld 点级世界映射
    ↓
Huber 鲁棒聚合得到毫米尺寸
    ↓
数值雅可比误差传播得到 sigma
    ↓
质量门控、显示、存储和导出
```

---

## 2. 适用场景

本项目适用于以下场景：

- 工业金属零件的非接触式尺寸测量。
- 零件在图像中存在一定旋转角度的测量任务。
- 需要实时显示像素尺寸、毫米尺寸、测量不确定度和有效扫描线数量的检测场景。
- 需要保存测量批次、逐帧明细和 CSV 导出的本地检测系统。
- 需要结合深度学习粗定位和传统视觉精密测量的工程项目。

需要注意的是，系统的最终绝对精度取决于相机安装、镜头、光源、标定板质量、标定覆盖范围、被测零件放置平面和边缘成像质量。软件链路可以提高稳定性和可解释性，但不能替代标准件或量块验证。

---

## 3. 仓库结构

```text
.
├── CMakeLists.txt                  # CMake 构建入口
├── README.md                       # 项目说明
├── 运行说明.md                     # 运行与排障说明
├── config/
│   ├── system.yaml                 # 主运行配置
│   └── classes.txt                 # 类别名称
├── camera/
│   ├── 相机内参.cal                # HALCON 相机内参文件
│   └── 相机位姿.dat                # HALCON 相机位姿文件
├── models/
│   └── best_opset12.onnx           # YOLO-OBB ONNX 模型
├── include/                        # 公共头文件
├── src/
│   ├── main.cpp                    # 命令行入口
│   ├── config.cpp                  # 配置解析
│   ├── camera_provider.cpp         # 工业相机 / 视频 / 合成源采集
│   ├── onnx_inferencer.cpp         # ONNXRuntime 推理与 Rotated NMS
│   ├── tracker_ekf.cpp             # OBB EKF 跟踪
│   ├── subpixel_caliper.cpp        # 多扫描线亚像素卡尺
│   ├── calibration.cpp             # HALCON 标定与 pixelToWorld
│   ├── measurement_uncertainty.cpp # 数值雅可比与误差传播
│   ├── logger.cpp                  # 日志输出
│   ├── app/                        # 应用编排与帧处理
│   └── desktop/                    # Qt 桌面端
├── scripts/
│   ├── run_demo.sh                 # 命令行运行脚本
│   ├── run_desktop.sh              # 桌面端运行脚本
│   ├── install_desktop_launcher.sh # 桌面启动器安装脚本
│   └── check_camera.sh             # 相机排障脚本
└── docs/
    ├── architecture.md             # 架构分层说明
    └── system_analysis_report.md   # 系统分析报告
```

---

## 4. 外部依赖

运行本项目需要在目标机器上安装以下依赖：

- CMake
- C++17 编译器
- OpenCV
- HALCON
- Hikrobot MVS SDK
- ONNX Runtime GPU 版本
- CUDA / cuDNN
- Qt Widgets / Qt Sql 开发包，桌面端需要

其中，生产运行建议启用：

```text
USE_HALCON=ON
USE_MVS=ON
USE_ONNXRUNTIME=ON
BUILD_DESKTOP=ON
```

当前工程要求 ONNXRuntime CUDA Execution Provider 可用。如果 CUDA EP 启用失败，程序会直接退出，避免误用 CPU 推理导致实时性不足。

---

## 5. 快速运行

### 5.1 命令行测量程序

在项目根目录执行：

```bash
bash scripts/run_demo.sh
```

首次运行或清理构建目录后，建议使用：

```bash
bash scripts/run_demo.sh --rebuild
```

指定配置文件运行：

```bash
bash scripts/run_demo.sh --config config/system.yaml
```

---

### 5.2 Qt 桌面端

运行桌面端：

```bash
bash scripts/run_desktop.sh --rebuild
```

桌面端包含：

- 登录界面
- 实时测量画面
- 像素距离、毫米距离、sigma、scans 显示
- LOW QUALITY 状态提示
- 本地 SQLite 数据记录
- 历史批次查询
- 逐帧明细查看
- CSV 导出
- 用户管理和部分运行参数设置

首次启动会创建本地管理员账号，之后通过账号密码登录。系统支持 `admin` 和 `operator` 两类角色。

---

### 5.3 安装桌面启动器

确认桌面端已经构建成功后执行：

```bash
bash scripts/install_desktop_launcher.sh
```

安装完成后，桌面和应用菜单会出现 `Metal Metrology` 启动入口，可直接双击启动。

---

## 6. 手动环境配置示例

如果需要手动排障，可以参考下面的方式启动。请根据自己的安装路径修改 `HALCONROOT`。

```bash
export HALCONROOT=/path/to/MVTec/HALCON-xx.xx
export HALCONARCH=x64-linux
export LD_LIBRARY_PATH=$HALCONROOT/lib/$HALCONARCH:$LD_LIBRARY_PATH

source /opt/MVS/bin/set_env_path.sh

./build/metal_metrology config/system.yaml
```

如果 CMake 找不到 OpenCV，可以设置：

```bash
export OpenCV_DIR=/path/to/opencv/build
```

如果 CMake 找不到 ONNX Runtime，可以设置：

```bash
export ONNXRUNTIME_DIR=/path/to/onnxruntime
```

---

## 7. 关键配置说明

主配置文件位于：

```text
config/system.yaml
```

当前主要配置如下：

```yaml
app:
  input_source: "mvs:0"
  onnx_model_path: "models/best_opset12.onnx"
  class_names_path: "config/classes.txt"
  calibration_file: "../camera"

  focal_length_mm: 8.0
  pixel_size_um: 4.0

  frame_width: 1280
  frame_height: 1024
  max_frames: -1
  queue_size: 6

  infer_interval: 2
  measure_interval: 2
  render_interval: 2

  conf_threshold: 0.35
  nms_threshold: 0.30

  caliper_length: -1.0
  caliper_half_width: 10.0
  gaussian_sigma: 1.2
  caliper_search_scale: 1.25
  use_obb_adaptive_caliper: 1
  measure_long_edge: 1
  multi_scan_count: 9
  edge_refine_half_window: 5
  edge_power_gamma: 2.0
  huber_delta_mm: 0.08

  estimate_measurement_uncertainty: 1
  max_sigma_mm: 0.10
  min_valid_scan_count: 5
  max_frame_jump_mm: 2.00
  min_edge_length_ratio: 0.82
  fallback_to_abs_gradient: 1

  show_window: 1
  display_max_width: 1280
  display_max_height: 760
  strict_calibration: 1
```

参数含义：

| 参数 | 含义 |
|---|---|
| `input_source` | 输入源，`mvs:0` 表示使用第 0 台 Hikrobot MVS 工业相机 |
| `onnx_model_path` | YOLO-OBB ONNX 模型路径 |
| `calibration_file` | HALCON 标定文件目录 |
| `frame_width / frame_height` | 图像采集分辨率 |
| `queue_size` | 采集线程与处理线程之间的缓存队列长度 |
| `infer_interval` | 推理分频，控制多少帧执行一次 YOLO-OBB 推理 |
| `measure_interval` | 测量分频，控制多少帧执行一次精密测量 |
| `render_interval` | 渲染分频，控制多少帧刷新一次显示 |
| `conf_threshold` | 检测置信度阈值 |
| `nms_threshold` | 旋转 NMS 阈值 |
| `caliper_length` | 卡尺长度，`-1.0` 表示由 OBB 自适应 |
| `caliper_half_width` | 卡尺半宽 |
| `gaussian_sigma` | 一维灰度 profile 的高斯平滑系数 |
| `multi_scan_count` | 平行扫描线数量 |
| `edge_refine_half_window` | 亚像素边缘 refinement 窗口半径 |
| `edge_power_gamma` | 梯度加权指数 |
| `huber_delta_mm` | 世界平面 Huber 鲁棒聚合阈值 |
| `estimate_measurement_uncertainty` | 是否估计毫米测量不确定度 |
| `max_sigma_mm` | sigma 质量阈值 |
| `min_valid_scan_count` | 最小有效扫描线数量 |
| `max_frame_jump_mm` | 帧间跳变阈值 |
| `strict_calibration` | 是否强制要求 HALCON 毫米标定可用 |

---

## 8. 软件架构

系统代码按以下层级组织：

```text
入口层
  └── main.cpp / desktop main

应用编排层
  └── PipelineRunner / PipelineInit / FrameProcessor

采集层
  └── CameraProvider

推理层
  └── OnnxObbInferencer / Letterbox / Coordinate Restore / Rotated NMS

跟踪层
  └── ObbTracker EKF

测量层
  └── SubpixelCaliper / Scanline / Edge Refinement

标定层
  └── CalibrationMapper / HALCON pixelToWorld

误差与质量层
  └── Numerical Jacobian / Huber Aggregation / QualityGate

显示与存储层
  └── OpenCV Visualizer / Qt UI / SQLite / CSV Export
```

推荐保持单向依赖：

```text
入口层
  ↓
应用编排层
  ↓
初始化层 / 帧处理层
  ↓
领域能力层
  ↓
显示、存储和导出
```

领域能力层不应反向依赖 `src/app/*`，可视化层不应直接触发推理或相机采集。

---

## 9. 图像坐标与模型坐标

系统保持原始图像坐标系进行检测显示和精密测量。

YOLO-OBB 推理前，系统会将原图 letterbox 到模型输入尺寸。模型输出后，系统会将检测框从 letterbox 坐标反变换回原图坐标：

```text
模型输入坐标
    ↓
减去 letterbox padding
    ↓
除以缩放比例 scale
    ↓
恢复到原始图像坐标系
```

后续 EKF、扫描线生成、亚像素边缘提取、可视化叠加都基于原始图像坐标系进行。这样可以避免模型输入缩放和填充对测量坐标造成影响。

---

## 10. HALCON 标定与畸变处理

系统不对整幅图像进行 world-plane 重采样，也不依赖固定 `mm/px` 比例尺进行最终测量。

实际毫米测量流程为：

1. 在原始图像中提取每条扫描线的左右亚像素边缘点。
2. 对每个边缘点调用 HALCON `ImagePointsToWorldPlane`。
3. 将像素坐标映射到世界平面坐标，单位为 mm。
4. 在世界平面中计算左右边界距离。
5. 使用 Huber 权重对多条扫描线结果进行鲁棒聚合。

系统启动时会输出局部比例诊断，例如：

```text
[HALCON] 标定映射诊断: 图像中心附近 100px 对应约 xx mm
```

该信息只用于检查标定映射是否基本有效，不是最终尺寸换算使用的固定比例系数。最终测量仍然以每个边缘点的 `pixelToWorld` 结果为准。

HALCON `.cal/.dat` 文件读取时，系统会将中文路径或中文文件名复制到 `/tmp/hik_yoloobb_halcon_calib/` 下的 ASCII 文件名再加载，用于规避部分 HALCON C++ wrapper 对中文路径兼容性不稳定的问题。

---

## 11. 测量算法说明

### 11.1 YOLO-OBB 粗定位

YOLO-OBB 模型输出目标旋转框：

```text
[cx, cy, w, h, conf, cls, angle]
```

其中：

- `cx, cy`：目标中心点
- `w, h`：旋转框宽高
- `conf`：置信度
- `cls`：类别
- `angle`：旋转角度

系统会根据置信度阈值过滤检测结果，并使用 Rotated NMS 去除重复框。

---

### 11.2 EKF 跟踪

系统使用 EKF 对 OBB 中心和角度进行平滑。状态量为：

```text
x = [cx, cy, angle, vx, vy, vangle]
```

观测量为：

```text
z = [cx, cy, angle]
```

EKF 的作用：

- 减少检测框中心抖动。
- 减少角度抖动。
- 在非推理帧上提供短时预测框。
- 为后续边缘点协方差估计提供位姿协方差。

需要注意：当前系统在非推理帧或短时检测缺失时会使用 EKF 预测维持显示和测量连续性。如果生产现场需要更严格的目标丢失保护，建议增加连续丢失帧数阈值，超过阈值后标记为 `NO TARGET` 并暂停测量。

---

### 11.3 多扫描线亚像素卡尺

系统根据稳定后的 OBB 计算测量方向和扫描线方向，然后围绕目标生成多条平行扫描线。

当前默认使用：

```text
multi_scan_count = 9
```

每条扫描线执行：

1. 沿测量方向采样灰度 profile。
2. 在扫描线法向小范围内进行带宽平均。
3. 对一维 profile 进行高斯平滑。
4. 计算中心差分梯度。
5. 通过梯度加权 refinement 得到亚像素边缘位置。
6. 输出左右边缘点和边缘点像素协方差。

多扫描线可以降低单条扫描线受反光、污渍、局部缺口或阴影影响的风险。

---

### 11.4 世界平面聚合

系统不是先在像素域平均再统一换算，而是：

1. 每条扫描线的左右边缘点分别映射到世界平面。
2. 根据左右边界的平均方向确定世界测量轴。
3. 将每个世界点投影到测量轴上。
4. 对左右边界分别执行 Huber 加权均值。
5. 计算左右均值差，得到毫米尺寸。

这种方式可以更好地利用 HALCON 标定结果，减小视场不同区域的局部比例变化影响。

---

### 11.5 误差传播与 sigma

系统使用数值雅可比估计像素坐标到世界坐标的局部映射关系：

```text
J = d(world_x, world_y) / d(pixel_x, pixel_y)
```

然后将像素边缘协方差传播到世界平面：

```text
Σ_world = J * Σ_pixel * J^T
```

最后将世界平面协方差投影到测量轴上，得到毫米测量标准差估计：

```text
sigma_mm = sqrt(left_variance + right_variance)
```

`sigma` 不是绝对误差，也不能直接代表真实测量误差。它表示当前帧在算法内部估计出的不确定度，可用于质量判断和异常帧筛选。

---

## 12. 质量控制逻辑

系统根据以下条件判断当前帧是否为低质量帧：

| 质量原因 | 含义 |
|---|---|
| `LOW_SCANS` | 有效扫描线数量不足 |
| `HIGH_SIGMA` | 当前帧测量不确定度过大 |
| `FRAME_JUMP` | 当前测量值相对上一稳定结果跳变过大 |
| `NO_TARGET` | 没有有效目标，建议后续扩展 |
| `CALIBRATION_INVALID` | 标定不可用，严格模式下会阻止运行 |

当前主要质量阈值：

```yaml
max_sigma_mm: 0.10
min_valid_scan_count: 5
max_frame_jump_mm: 2.00
```

质量判断通过后：

```text
OK 帧 → 进入中值平滑 → 更新稳定结果 → 可参与批次统计
```

质量判断失败后：

```text
LOW QUALITY 帧 → 显示边缘点和原因 → 不进入最终平滑结果
```

桌面端可以将低质量帧作为逐帧明细保存，用于后续追溯和分析。但低质量帧不会参与 OK 帧均值统计。

---

## 13. 显示结果含义

实时窗口左上角或桌面端卡片会显示：

| 字段 | 含义 |
|---|---|
| `px` | 当前显示的像素距离 |
| `mm` | 当前显示的毫米尺寸 |
| `sigma` | 当前帧毫米测量不确定度估计 |
| `scans` | 有效参与聚合的扫描线数量 |
| `LOW QUALITY` | 当前帧没有通过质量门控 |
| `OK` | 当前帧通过质量判断 |

其中：

- `px` 和 `mm` 在 OK 帧中会进入 5 帧中值平滑。
- `sigma` 越小，表示系统估计当前帧越稳定。
- `scans` 越接近 `multi_scan_count`，说明有效边缘提取越充分。
- `LOW QUALITY` 不一定表示程序错误，可能是零件移动、反光、遮挡、边缘点不足或帧间跳变造成的。

---

## 14. 数据库与导出

桌面端使用 SQLite 保存本地测量记录，默认路径：

```text
data/metrology.db
```

数据库主要包含：

- 用户表
- 测量批次表
- 逐帧测量明细表

一次点击“开始”到“停止”会形成一条运行批次记录。每条批次下可以保存逐帧测量明细，包括：

- frame_id
- timestamp
- username
- px
- raw_mm
- mm
- sigma
- scans
- quality
- cx
- cy
- angle
- config_version
- calibration_file

历史页支持：

- 查询批次记录
- 查看逐帧明细
- 删除选中批次
- 一键清空历史
- 导出 CSV

导出逻辑：

- 选中某个批次时，导出该批次的逐帧明细。
- 未选中批次时，导出当前筛选条件下的批次汇总。

---

## 15. 常见问题

### 15.1 相机打不开

先执行：

```bash
bash scripts/check_camera.sh
```

检查内容包括：

- 相机连接是否正常。
- USB 供电是否稳定。
- Hikrobot MVS SDK 是否安装。
- 当前用户是否有相机访问权限。
- `input_source` 是否正确设置为 `mvs:0`。

---

### 15.2 GPU 推理未启用

如果日志提示 CUDA Execution Provider 启用失败，说明 ONNXRuntime GPU 环境存在问题。

需要检查：

- NVIDIA 驱动是否正常。
- CUDA / cuDNN 是否匹配。
- ONNXRuntime 是否为 GPU 版本。
- `LD_LIBRARY_PATH` 是否包含 ONNXRuntime 和 CUDA 相关库路径。
- CMake 是否启用了 `USE_ONNXRUNTIME=ON`。

当前生产配置要求 GPU 推理可用，不建议自动回退到 CPU 推理。

---

### 15.3 没有毫米值

检查：

- `strict_calibration` 是否为 `1`。
- `camera/相机内参.cal` 是否存在。
- `camera/相机位姿.dat` 是否存在。
- 标定文件是否来自当前相机、镜头和安装姿态。
- 被测零件是否放置在标定对应的世界平面上。

如果 HALCON 标定不可用且关闭 strict 模式，系统仍可显示像素尺寸，但毫米结果不可用。

---

### 15.4 HALCON 授权过期

如果日志出现：

```text
HALCON error #2042: Feature has expired
```

说明 HALCON 授权功能过期，需要恢复 HALCON license。重新编译程序不能解决该问题。

---

### 15.5 画面显示不完整

如果 Ubuntu 小屏幕只能看到窗口上半部分，可以调小：

```yaml
display_max_height: 700
```

或：

```yaml
display_max_height: 650
```

程序会保持图像比例自动缩放。

---

### 15.6 LOW QUALITY 频繁出现

可能原因：

- 零件还在移动。
- 金属表面反光严重。
- 光源不均匀。
- 边缘对比度不足。
- 扫描线有效数量太少。
- sigma 阈值设置过严。
- 标定平面和实际测量平面不一致。
- YOLO-OBB 检测框角度抖动较大。
- 零件位于图像边缘区域，畸变残差或边缘提取误差更明显。

建议排查顺序：

1. 固定零件，保证静止。
2. 调整光源，减少强反光。
3. 检查 OBB 是否稳定包围目标。
4. 检查边缘点是否落在真实边缘上。
5. 观察 `scans` 是否低于阈值。
6. 观察 `sigma` 是否长期偏高。
7. 使用标准件验证不同区域测量误差。
8. 必要时重新标定相机。

---

## 16. 当前交付边界

本仓库主要包含生产推理和测量链路，不包含：

- YOLO-OBB 模型训练代码
- 数据集制作代码
- 单元测试目标
- 开发调试构建产物
- 标准件误差统计数据
- 多工位生产线调度系统

当前版本适合作为工业视觉测量系统的工程原型和生产运行版基础。若要用于正式生产，需要进一步完成：

1. 标准件或量块精度验证。
2. 多位置、多角度重复性测试。
3. 光源和安装结构固定。
4. 连续目标丢失保护。
5. 低质量帧策略完善。
6. 标定残差分析和必要的残差补偿。
7. 长时间运行稳定性测试。

---

## 17. 建议验证指标

正式使用前建议记录 30 到 100 帧稳定测量数据，并统计：

- 平均测量值
- 平均误差
- 最大误差
- 重复性标准差
- OK 帧比例
- LOW QUALITY 帧比例
- `sigma` 与实际误差的相关性
- 不同图像区域的测量差异
- 不同旋转角度下的测量差异

建议使用标准件或量块作为真实尺寸参考，不建议只根据单次显示值判断系统精度。

---

## 18. 后续优化方向

后续可以继续优化以下内容：

1. **目标丢失保护**  
   增加连续未检测帧数统计，超过阈值后标记 `NO TARGET`，禁止继续使用 EKF 预测框测量。

2. **边缘提取优化**  
   针对金属反光、边缘模糊和局部缺口，引入更严格的边缘质量评分和异常扫描线剔除。

3. **畸变残差补偿**  
   在 HALCON 标定基础上，使用标准件测量残差建立位置相关补偿模型。

4. **质量门控细化**  
   将 `LOW_SCANS / HIGH_SIGMA / FRAME_JUMP / NO_TARGET / EDGE_WEAK` 等原因进一步细分。

5. **多类型零件适配**  
   针对不同零件建立不同的类别、测量边、卡尺参数和质量阈值。

6. **日志系统增强**  
   增加模块标签、日志等级和运行日志文件，方便现场排障。

7. **部署打包**  
   将 HALCON、MVS、ONNXRuntime 和 Qt 运行环境整理为更标准的部署说明或安装脚本。

---

## 19. 核心结论

本项目的核心思想是：

```text
YOLO-OBB 负责目标粗定位和姿态估计，
亚像素卡尺负责真实边缘精测量，
HALCON 标定负责像素点到世界平面的毫米映射，
Huber 聚合和 sigma 质量控制负责提高结果稳定性和可解释性。
```

系统最终尺寸不是由检测框宽高直接给出，而是由多条扫描线的亚像素边缘点在世界平面中鲁棒聚合得到。这样既利用了深度学习对复杂姿态的定位能力，也保留了传统视觉测量在边缘定位和几何计算上的可解释性。
