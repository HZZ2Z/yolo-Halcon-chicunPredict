#include "app/pipeline_init.hpp"

#include "app/frame_visualizer.hpp"
#include "calibration.hpp"
#include "camera_provider.hpp"
#include "logger.hpp"
#include "onnx_inferencer.hpp"
#include "spatial_error_compensator.hpp"
#include "subpixel_caliper.hpp"
#include "tracker_ekf.hpp"

#include <memory>

bool InitializePipeline(const AppConfig& cfg, PipelineContext& context) {
    context.calibration = std::make_unique<CalibrationMapper>();
    const bool calibration_loaded =
        (!cfg.calibration_file.empty() &&
         context.calibration->load(cfg.calibration_file,
                                   cfg.frame_width,
                                   cfg.frame_height,
                                   cfg.focal_length_mm,
                                   cfg.pixel_size_um));
    if (!calibration_loaded) {
        logger::Warn("警告: 标定文件加载失败");
    }
    if (cfg.strict_calibration && !context.calibration->canMeasureInMm()) {
        logger::Error("错误: strict_calibration=1 但当前标定无法完成毫米换算。");
        logger::Error("HALCON 模式下请提供有效的相机参数(.cal)与位姿文件(.dat)。");
        return false;
    }
    if (context.calibration->canMeasureInMm()) {
        context.calibration->logScaleDiagnostics(cfg.frame_width, cfg.frame_height);
    }

    context.camera = std::make_unique<CameraProvider>(cfg.input_source, cfg.frame_width, cfg.frame_height);
    if (!context.camera->open()) {
        logger::Error(std::string("相机/视频源打开失败: ") + cfg.input_source);
        logger::Warn("提示: 若系统无 /dev/video*，说明设备可能不是 UVC 直出。");
        logger::Warn("请先运行 scripts/check_camera.sh 诊断；若为 Hikrobot 工业相机，建议接入 MVS SDK 采集。");
        return false;
    }

    context.inferencer = std::make_unique<OnnxObbInferencer>(
        cfg.onnx_model_path, cfg.conf_threshold, cfg.nms_threshold);
    if (!context.inferencer->init()) {
        logger::Error("ONNX 初始化失败：请确认已使用 USE_ONNXRUNTIME=ON 编译，且模型路径正确。");
        return false;
    }

    context.caliper = std::make_unique<SubpixelCaliper>(cfg.caliper_length,
                                                        cfg.caliper_half_width,
                                                        cfg.gaussian_sigma,
                                                        cfg.caliper_search_scale,
                                                        cfg.use_obb_adaptive_caliper,
                                                        cfg.measure_long_edge,
                                                        cfg.multi_scan_count,
                                                        cfg.edge_refine_half_window,
                                                        cfg.edge_power_gamma,
                                                        cfg.min_edge_length_ratio,
                                                        cfg.fallback_to_abs_gradient);
    context.compensator = std::make_unique<SpatialErrorCompensator>();
    if (!cfg.residual_compensation_file.empty()) {
        if (!context.compensator->load(cfg.residual_compensation_file)) {
            logger::Warn("警告: 残差补偿文件加载失败，当前运行不启用空间残差补偿。");
        }
    }
    context.tracker = std::make_unique<ObbTracker>();
    context.visualizer = std::make_unique<FrameVisualizer>();

    return true;
}
