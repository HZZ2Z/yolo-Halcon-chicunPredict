# 开发者导引（清晰 + 可测试）

## 1. 代码分层约定

- `include/`：对外头文件与公共数据结构。
- `src/`：领域实现（采集、推理、跟踪、标定、测量、配置、日志）。
- `src/app/`：应用编排（初始化、单帧处理、运行循环、可视化）。
- `tests/`：无硬件依赖的单元测试。

建议依赖方向：`app -> core/foundation`，避免 `core` 反向依赖 `app`。

## 2. CMake 目标结构

- `metal_foundation`：配置与日志等基础能力（便于单测）。
- `metal_core`：领域能力实现（采集/推理/测量/跟踪/标定）。
- `metal_app`：应用编排。
- `metal_metrology`：最终可执行程序。
- `metal_unit_tests`：单元测试可执行文件（CTests）。

## 3. 测试策略（先低耦合，再扩展）

第一优先测试：
- `LoadConfig`：参数边界、路径重定向、异常信息。
- `ThreadSafeQueue`：顺序语义、关闭语义。

后续扩展建议：
- 为 `frame_processor` 提供无 UI、可注入 mock 的处理入口。
- 将耗时统计（推理/测量/渲染）抽离成独立模块并单测。

## 4. 本地测试命令

```bash
cmake -S . -B build_test \
  -DCMAKE_BUILD_TYPE=Release \
  -DUSE_MVS=OFF -DUSE_HALCON=OFF -DUSE_ONNXRUNTIME=OFF
cmake --build build_test -j
ctest --test-dir build_test --output-on-failure
```

> 说明：测试默认不依赖工业相机、HALCON、ONNXRuntime，可用于快速回归。
