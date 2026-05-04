# 九宫格评估工具

`tools/` 是离线/半自动工具目录，不属于实时生产主链路。

这些工具不会被 `CMakeLists.txt` 编译进 `metal_metrology`，也不会修改正式 `config/system.yaml`、`camera/`、`models/`。生产运行仍然使用：

```bash
bash scripts/run_demo.sh
bash scripts/run_demo.sh --rebuild
```

## 目录

- `grid_capture/run_grid_batch.sh`：按九宫格顺序批量采集 9 个位置。
- `grid_capture/run_grid_capture.sh`：按单个九宫格位置采集测量 CSV。
- `grid_analysis/analyze_grid_csv.py`：统计九宫格误差并生成残差补偿表。
- `grid_analysis/run_compensation_test.sh`：用临时配置验证主链路残差补偿效果。

不要用 `run_grid_capture.sh` 做完整九宫格，它只采一个位置。完整九宫格请使用 `run_grid.sh`。

## 推荐流程

1. 准备标准件，确认真实尺寸。
2. 运行批量采集工具，按提示依次完成 9 个位置：

```bash
bash tools/grid_capture/run_grid.sh --true-mm 33.00 --ok-frames 50 --min-ok-frames 30 --rebuild
```

默认顺序：

```text
LT  CT  RT
LC   C  RC
LB  CB  RB
```

每个位置会先进入实时预览；摆放确认后，在图像窗口按 `q` 或 `Esc` 退出预览，再按终端提示回车开始采集。当前位置采集到 50 条 `quality=OK` 有效帧后会自动结束并提示下一个位置；如果只达到 30-49 条，会标记为 `WARN` 并继续采下一个位置；低于 30 条会标记为 `FAIL`，批量流程默认仍继续，最后统一报告。

3. 工具默认输出带时间戳的新文件，避免和旧实验混在一起：

```text
measurements/grid_raw_YYYYmmdd_HHMMSS.csv
measurements/grid_summary_YYYYmmdd_HHMMSS.csv
measurements/residual_grid_YYYYmmdd_HHMMSS.csv
```

九宫格汇总 CSV 默认只保存 `quality=OK` 的有效帧。单次临时 CSV 仍写在 `/tmp`，用于排查低质量原因。

高级调试：如果只想采一个位置：

```bash
bash tools/grid_capture/run_grid.sh --single-position C --true-mm 33.00 --ok-frames 50
```

如果已经摆好零件，只想直接采集：

```bash
bash tools/grid_capture/run_grid.sh --single-position C --true-mm 33.00 --ok-frames 50 --no-preview
```

如果想强制重编译后再采集：

```bash
bash tools/grid_capture/run_grid.sh --single-position C --true-mm 33.00 --ok-frames 50 --rebuild
```

4. 统计所有采集结果：

```bash
python3 tools/grid_analysis/analyze_grid_csv.py \
  --input measurements/grid_raw_YYYYmmdd_HHMMSS.csv \
  --summary measurements/grid_summary_YYYYmmdd_HHMMSS.csv \
  --residual measurements/residual_grid_YYYYmmdd_HHMMSS.csv
```

5. 查看 `grid_summary.csv`：

- 同一位置 `mm_std` 小：重复性好。
- 不同位置 `error_mean` 差异大：存在空间残差。
- `px_std` 大：边缘提取不稳定。
- `mm_per_px_mean` 差异大：空间映射/标定平面需要重点检查。

`residual_grid.csv` 只作为可选补偿表。确认结果可靠后，才手动填入 `config/system.yaml` 的 `residual_compensation_file`。

工具输出限制在 `measurements/` 或 `/tmp`。如果要重新做一轮完整实验，可以先换一个 `--output measurements/grid_raw_日期.csv`，避免和旧数据混在一起。

## 主链路补偿测试

补偿功能已经接入主程序，但正式配置默认关闭。测试时请使用临时配置，不要直接修改 `config/system.yaml`：

```bash
bash tools/grid_analysis/run_compensation_test.sh \
  --residual measurements/residual_grid_YYYYmmdd_HHMMSS.csv \
  --true-mm 33.00 \
  --frames 1200
```

脚本会生成 `/tmp/hik_comp_test.yaml`，运行主程序并输出：

```text
measurements/comp_test_YYYYmmdd_HHMMSS.csv
```

同位置做补偿前对照时运行：

```bash
bash tools/grid_analysis/run_compensation_test.sh --mode baseline --true-mm 33.00 --frames 1200
```

测试 CSV 中 `correction_mm` 应为非零；脚本会汇总补偿前后的均值、标准差和误差。若残差表缺少某个九宫格位置，或最大补偿量超过 `0.50 mm`，脚本会打印警告；这种补偿表只建议用于诊断，不建议直接写入生产配置。

九宫格引导由临时配置项 `show_grid_guide` 和 `grid_guide_position` 控制，只在工具生成的 `/tmp` 配置中开启。正式 `bash scripts/run_demo.sh` 不显示九宫格。

预览阶段会临时放宽 `max_frame_jump_mm`，避免摆放移动时持续出现 `FRAME_JUMP`。采集阶段仍使用生产质量阈值；若出现 `LOW QUALITY`，画面会显示 `LOW_SCANS`、`HIGH_SIGMA` 或 `FRAME_JUMP` 的具体数值和处理建议。

批量工具默认单轮尝试 `1200` 帧，以减少频繁重启程序。若需要严格要求每个位置都达到 50 条 OK 后才继续，可加 `--strict`。

显示窗口会按 `display_max_width/display_max_height` 保持比例缩放。如果 Ubuntu 屏幕高度不够，可以在 `config/system.yaml` 里把 `display_max_height` 调到 `700` 或 `650`。
