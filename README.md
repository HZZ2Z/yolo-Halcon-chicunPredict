# 工业金属零件视觉测量系统（训练外完整实现）

本项目提供金属零件在线视觉测量的完整推理侧链路（不含模型训练），重点面向 **Hikrobot 工业相机 + YOLO OBB ONNX + 高性能实时处理**。

当前版本已按性能优先策略优化：
- 默认 `Release -O3` 编译
- ONNXRuntime **强制优先 CUDA Execution Provider**（启用失败直接报错退出，避免误用 CPU）
- 推理/测量/渲染分频执行（可配置）
- 轻量化主流程与实时尺寸显示（无磁盘写盘）

## 开发者入口

- 架构说明：`docs/architecture.md`
- 开发导引：`docs/development.md`
- 单元测试：`tests/`

快速执行单元测试：

```bash
cmake -S . -B build_test \
  -DCMAKE_BUILD_TYPE=Release \
  -DUSE_MVS=OFF -DUSE_HALCON=OFF -DUSE_ONNXRUNTIME=OFF
cmake --build build_test -j
ctest --test-dir build_test --output-on-failure
```

---

## 1. 系统架构

### 1.1 模块分层
- 采集层：`src/camera_provider.cpp`
  - 支持 `mvs:N`（MVS SDK）、UVC 索引（`"0"`）和视频文件路径
  - 支持空输入源（`""`）的合成数据模式用于离线调试
- 推理层：`src/onnx_inferencer.cpp`
  - 预处理（letterbox + blob）
  - ONNX 推理（CUDA EP）
  - OBB 解码 + Rotated NMS
- 稳定层：`src/tracker_ekf.cpp`
  - 卡尔曼滤波跟踪中心与角度，降低检测抖动
- 测量层：`src/subpixel_caliper.cpp`
  - 基于 OBB 主轴方向构建 1D profile，做亚像素边缘定位
- 标定层：`src/calibration.cpp`
  - 畸变矫正 + 单应性像素到毫米映射
- 应用层：`src/app/pipeline_runner.cpp`
  - 生产者/消费者并发队列与线程调度壳层
- 初始化层：`src/app/pipeline_init.cpp`
  - 采集、标定、推理、测量组件初始化与前置校验
- 帧处理层：`src/app/frame_processor.cpp`
  - 单帧去畸变、推理、跟踪、测量、平滑与渲染触发
- 可视化层：`src/app/frame_visualizer.cpp`
  - 统一绘制 OBB/测量结果与键盘退出处理
- 日志层：`src/logger.cpp`
  - 统一 Info/Warn/Error 输出与诊断日志开关（入口、应用、ONNX、HALCON 模块已接入）
- 入口层：`src/main.cpp`
  - 仅负责参数读取、配置加载与异常处理

### 1.2 数据流
1. 采集线程读取图像并入队
2. 消费线程出队并执行去畸变
3. 按 `infer_interval` 控制是否进行一次完整推理
4. 卡尔曼更新得到稳定 OBB
5. 按 `measure_interval` 执行亚像素测量
6. 若有标定则输出毫米距离，否则输出 `-1`
7. 按 `render_interval` 进行 UI 渲染

---

## 2. 关键算法说明

### 2.1 OBB 推理与解码
位置：`src/onnx_inferencer.cpp`

- 输入采用 letterbox 到模型输入尺寸（当前默认 1280）
- 使用 `cv::dnn::blobFromImage` 生成 `NCHW float32`，减少预处理开销
- 输出按固定格式解析：`[cx, cy, w, h, conf, cls, angle]`
- 通过 letterbox 反变换把框映射回原图坐标

### 2.2 Rotated NMS
- 对每类框按置信度排序
- 使用 `cv::rotatedRectangleIntersection` 求交并计算旋转 IoU
- IoU 超过阈值（`nms_threshold`）即抑制

### 2.3 卡尔曼跟踪
位置：`src/tracker_ekf.cpp`

- 状态维度 6：`[x, y, angle, vx, vy, vangle]`
- 观测维度 3：`[x, y, angle]`
- 在不推理的帧上，依靠预测状态维持连续性（提升吞吐）

### 2.4 亚像素卡尺测量
位置：`src/subpixel_caliper.cpp`

- 沿 OBB 切向建立采样线
- 在法向做带宽平均得到 1D 亮度 profile
- 高斯平滑后取梯度
- 左半区找最小梯度、右半区找最大梯度
- 用 3 点抛物线做亚像素峰值修正
- 计算两边缘点距离得到像素尺寸

### 2.5 标定与毫米换算
位置：`src/calibration.cpp`

- 若有 `camera_matrix + dist_coeffs`：先去畸变
- 若有 `homography`：`pixelToWorld()` 映射到世界平面
- 若无 `homography`，但有 `camera_matrix + rvec + tvec`：通过针孔模型反投影到 `Z=0` 工件平面
- `world_distance_mm = ||w1 - w0||`

---

## 3. 性能策略（重点）

### 3.1 已启用的性能机制
- Release 优化编译（`-O3 -DNDEBUG`）
- CUDA EP 推理（非 CPU 回退）
- 分频执行：推理 / 测量 / 渲染可独立降频
- 关闭 CSV 持续写盘，避免 I/O 影响实时性
- 队列限长，降低实时延迟感

### 3.2 关键性能参数
配置文件：`config/system.yaml`

- `infer_interval`：每 N 帧推理一次（建议 1~3）
- `measure_interval`：每 N 帧测量一次（建议 1~3）
- `render_interval`：每 N 帧渲染一次（建议 1~4）
- `queue_size`：队列长度（建议 4~10）
- `conf_threshold`：提高可减少候选框与 NMS 开销

推荐（RTX 3060）：
- 平衡模式：`infer=2, measure=2, render=2`
- 极限吞吐：`infer=3, measure=2, render=3, show_window=0`

---

## 4. 依赖与环境

### 4.1 必需依赖
- C++17 编译器（g++/clang++）
- CMake >= 3.16
- OpenCV

### 4.2 可选/增强依赖
- Hikrobot MVS SDK（工业相机采集）
- ONNXRuntime C++（推理）
- NVIDIA CUDA/cuDNN（ONNX GPU 加速）

---

## 5. 构建与运行

### 5.1 推荐构建（MVS + ONNX + CUDA）

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DUSE_MVS=ON -DMVS_DIR=/opt/MVS \
  -DUSE_ONNXRUNTIME=ON \
  -DONNXRUNTIME_INCLUDE_DIR=/home/hzwang/onnxruntime/include \
  -DONNXRUNTIME_LIB_DIR=/home/hzwang/onnxruntime/build/Linux/Release

cmake --build build -j
```

### 5.2 运行

```bash
./build/metal_metrology config/system.yaml
```

或使用脚本：

```bash
bash scripts/run_demo.sh
```

提示：请直接复制代码块中的纯命令，不要输入成 `bash [run_demo.sh](...)` 这种 Markdown 链接形式。

说明：快速模式会跳过编译；若当前二进制不是以 `USE_ONNXRUNTIME=ON` 构建，程序会在 ONNX 初始化阶段退出，请使用 `--rebuild`。

### 5.3 运行配置说明
- `input_source: "mvs:0"`：MVS 第 0 台相机
- `input_source: "0"`：UVC 摄像头
- `input_source: "/path/video.mp4"`：视频文件
- `input_source: ""`：合成源（调试）

---

## 6. 配置项说明（app 节点）

- `onnx_model_path`：OBB ONNX 模型路径
- `calibration_file`：标定文件或目录（例如 `camera`）
- `focal_length_mm / pixel_size_um`：当仅有位姿文件（`.dat`）时用于构造内参的兜底光学参数
- `frame_width / frame_height`：采集分辨率
- `max_frames`：>0 固定帧数；<=0 持续运行
- `queue_size`：采集缓冲队列长度
- `infer_interval`：推理分频
- `measure_interval`：测量分频
- `render_interval`：渲染分频
- `conf_threshold / nms_threshold`：检测后处理阈值
- `caliper_length / caliper_half_width / gaussian_sigma`：卡尺参数
- `show_window`：是否显示窗口
- `enable_diag_logs`：诊断日志开关

---

## 7. 输出结果

当前版本输出以实时显示为主：
- 窗口显示稳定 OBB、左右边缘点、像素尺寸与毫米尺寸
- 未提供标定时，毫米显示为 `mm=N/A`

如需重新启用 CSV 记录，建议在 `src/app/frame_processor.cpp` 中按测量结果分支扩展写盘逻辑。

---

## 8. 常见问题

### Q1：看起来还是卡
- 先确认是 Release 构建
- 降低 `render_interval` 频率（增大数值）
- 尝试 `show_window=0` 验证纯处理吞吐
- 提高 `conf_threshold`，减少候选框数量

### Q2：如何判断相机链路问题
- 使用：`scripts/check_camera.sh`
- 如果无 `/dev/video*`，优先使用 `mvs:N` 路径

### Q3：GPU 没生效怎么办
- 检查 ONNXRuntime 是否包含 CUDA provider
- 检查 `libonnxruntime_providers_cuda.so` 是否在运行库路径
- 当前程序在 CUDA EP 启用失败时会直接退出

---

## 9. 代码入口索引

- 主流程编排：`src/app/pipeline_runner.cpp`
- 初始化与预检：`src/app/pipeline_init.cpp`
- 单帧处理：`src/app/frame_processor.cpp`
- 可视化：`src/app/frame_visualizer.cpp`
- 日志：`src/logger.cpp`
- 程序入口：`src/main.cpp`
- 配置加载：`src/config.cpp`
- 日志门面：`src/logger.cpp`
- 采集：`src/camera_provider.cpp`
- 推理与 NMS：`src/onnx_inferencer.cpp`
- 跟踪：`src/tracker_ekf.cpp`
- 标定：`src/calibration.cpp`
- 亚像素测量：`src/subpixel_caliper.cpp`

---

## 10. 项目边界

本仓库只覆盖推理与测量工程链路，不包含模型训练代码。