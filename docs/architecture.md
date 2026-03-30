# 项目架构分层说明

## 目标

在不改变现有功能、算法链路与性能策略的前提下，降低入口文件复杂度，明确模块职责，提升后续可维护性与多人协作可读性。

## 当前分层

- 入口层
  - `src/main.cpp`
  - 职责：参数读取、配置加载、统一异常边界。

- 应用编排层
  - `src/app/pipeline_runner.cpp`
  - 职责：生产者/消费者线程创建、队列生命周期、退出收敛。

- 初始化层
  - `src/app/pipeline_init.cpp`
  - 职责：
    - 初始化 `CameraProvider`
    - 加载并校验 `CalibrationMapper`
    - 初始化 `OnnxObbInferencer`
    - 装配 `SubpixelCaliper`、`ObbTracker`、`FrameVisualizer`

- 帧处理层
  - `src/app/frame_processor.cpp`
  - 职责：
    - 单帧去畸变
    - 按分频执行推理
    - 跟踪融合
    - 按分频执行测量
    - 毫米换算与中值平滑
    - 按分频触发可视化

- 可视化层
  - `src/app/frame_visualizer.cpp`
  - 职责：渲染 OBB、测量点、文本信息；处理键盘退出。

- 日志层
  - `src/logger.cpp`
  - 职责：统一 Info/Warn/Error 输出入口，支持诊断日志开关。
  - 现状：入口层、应用层、以及 `onnx_inferencer/calibration` 领域模块均已接入该门面。

- 领域能力层（原有模块）
  - 采集：`src/camera_provider.cpp`
  - 推理：`src/onnx_inferencer.cpp`
  - 跟踪：`src/tracker_ekf.cpp`
  - 标定：`src/calibration.cpp`
  - 测量：`src/subpixel_caliper.cpp`
  - 配置：`src/config.cpp`

## 依赖方向（约束）

建议保持单向依赖：

1. 入口层 -> 应用编排层
2. 应用编排层 -> 初始化层 / 帧处理层
3. 初始化层 / 帧处理层 -> 领域能力层
4. 可视化层仅依赖配置与数据类型，不反向依赖编排层

禁止反向依赖：

- 领域能力层不依赖 `src/app/*`
- 初始化层不包含业务帧循环
- 可视化层不直接触发推理或相机采集

## 性能一致性说明

重构仅调整文件组织与调用边界，以下行为保持不变：

- 生产者/消费者并发模型
- 队列长度与背压策略
- 推理/测量/渲染分频逻辑
- 中值平滑窗口（5）
- 标定毫米换算路径

## 后续演进建议

- 若后续需要单元测试，可优先为 `frame_processor` 增加无 UI 的处理测试入口。
- 若需要多工位扩展，可在应用编排层引入多 pipeline 实例管理，不下沉到领域层。
- 若需要更细粒度日志，可在 `logger` 中增加模块标签与日志级别过滤（保持零业务侵入）。
