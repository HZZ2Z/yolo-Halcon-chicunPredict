#include "config.hpp"

#include <cmath>
#include <filesystem>
#include <opencv2/opencv.hpp>
#include <stdexcept>

namespace {

template <typename T>
void readIfExists(const cv::FileNode& node, const char* key, T& value) {
    if (!node[key].empty()) {
        node[key] >> value;
    }
}

void requireRange(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error("配置非法: " + message);
    }
}

void validateConfig(const AppConfig& config) {
    requireRange(config.frame_width > 0, "frame_width 必须 > 0");
    requireRange(config.frame_height > 0, "frame_height 必须 > 0");
    requireRange(config.queue_size > 0, "queue_size 必须 > 0");
    requireRange(config.infer_interval > 0, "infer_interval 必须 > 0");
    requireRange(config.measure_interval > 0, "measure_interval 必须 > 0");
    requireRange(config.render_interval > 0, "render_interval 必须 > 0");

    requireRange(config.conf_threshold > 0.0f && config.conf_threshold <= 1.0f,
                 "conf_threshold 必须在 (0, 1] 区间");
    requireRange(config.nms_threshold > 0.0f && config.nms_threshold <= 1.0f,
                 "nms_threshold 必须在 (0, 1] 区间");

    if (config.use_obb_adaptive_caliper) {
        requireRange(std::isfinite(config.caliper_length),
                     "caliper_length 必须为有限数值");
    } else {
        requireRange(config.caliper_length > 0.0f,
                     "caliper_length 必须 > 0，或启用 use_obb_adaptive_caliper");
    }
    requireRange(config.caliper_half_width > 0.0f, "caliper_half_width 必须 > 0");
    requireRange(config.gaussian_sigma > 0.0f, "gaussian_sigma 必须 > 0");
    requireRange(config.caliper_search_scale > 0.0f, "caliper_search_scale 必须 > 0");
    requireRange(config.multi_scan_count > 0, "multi_scan_count 必须 > 0");
    requireRange(config.edge_refine_half_window > 0, "edge_refine_half_window 必须 > 0");
    requireRange(config.edge_power_gamma > 0.0f, "edge_power_gamma 必须 > 0");
    requireRange(config.huber_delta_mm > 0.0f, "huber_delta_mm 必须 > 0");
    requireRange(config.max_sigma_mm > 0.0f, "max_sigma_mm 必须 > 0");
    requireRange(config.min_valid_scan_count >= 0, "min_valid_scan_count 必须 >= 0");
    requireRange(config.max_frame_jump_mm > 0.0f, "max_frame_jump_mm 必须 > 0");
    requireRange(config.min_edge_length_ratio > 0.0f && config.min_edge_length_ratio <= 1.0f,
                 "min_edge_length_ratio 必须在 (0, 1] 区间");
    requireRange(config.standard_true_mm < 0.0f || config.standard_true_mm > 0.0f,
                 "standard_true_mm 必须 > 0，或设置为负数表示未知");
    requireRange(config.display_max_width > 0, "display_max_width 必须 > 0");
    requireRange(config.display_max_height > 0, "display_max_height 必须 > 0");
}

} // namespace

AppConfig LoadConfig(const std::string& yaml_path) {
    cv::FileStorage fs(yaml_path, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        throw std::runtime_error("无法打开配置文件: " + yaml_path);
    }

    AppConfig config;
    const cv::FileNode root = fs["app"];
    if (root.empty()) {
        throw std::runtime_error("配置文件缺少 app 节点");
    }

    readIfExists(root, "input_source", config.input_source);
    readIfExists(root, "onnx_model_path", config.onnx_model_path);
    readIfExists(root, "class_names_path", config.class_names_path);
    readIfExists(root, "calibration_file", config.calibration_file);
    readIfExists(root, "focal_length_mm", config.focal_length_mm);
    readIfExists(root, "pixel_size_um", config.pixel_size_um);

    readIfExists(root, "frame_width", config.frame_width);
    readIfExists(root, "frame_height", config.frame_height);
    readIfExists(root, "max_frames", config.max_frames);
    readIfExists(root, "queue_size", config.queue_size);
    readIfExists(root, "infer_interval", config.infer_interval);
    readIfExists(root, "measure_interval", config.measure_interval);
    readIfExists(root, "render_interval", config.render_interval);

    config.infer_interval = std::max(1, config.infer_interval);
    config.measure_interval = std::max(1, config.measure_interval);
    config.render_interval = std::max(1, config.render_interval);

    readIfExists(root, "conf_threshold", config.conf_threshold);
    readIfExists(root, "nms_threshold", config.nms_threshold);

    readIfExists(root, "caliper_length", config.caliper_length);
    readIfExists(root, "caliper_half_width", config.caliper_half_width);
    readIfExists(root, "gaussian_sigma", config.gaussian_sigma);
    readIfExists(root, "caliper_search_scale", config.caliper_search_scale);

    int use_obb_adaptive_caliper = config.use_obb_adaptive_caliper ? 1 : 0;
    readIfExists(root, "use_obb_adaptive_caliper", use_obb_adaptive_caliper);
    config.use_obb_adaptive_caliper = use_obb_adaptive_caliper != 0;

    int measure_long_edge = config.measure_long_edge ? 1 : 0;
    readIfExists(root, "measure_long_edge", measure_long_edge);
    config.measure_long_edge = measure_long_edge != 0;

    readIfExists(root, "multi_scan_count", config.multi_scan_count);
    readIfExists(root, "edge_refine_half_window", config.edge_refine_half_window);
    readIfExists(root, "edge_power_gamma", config.edge_power_gamma);
    readIfExists(root, "huber_delta_mm", config.huber_delta_mm);

    int estimate_measurement_uncertainty = config.estimate_measurement_uncertainty ? 1 : 0;
    readIfExists(root, "estimate_measurement_uncertainty", estimate_measurement_uncertainty);
    config.estimate_measurement_uncertainty = estimate_measurement_uncertainty != 0;

    readIfExists(root, "max_sigma_mm", config.max_sigma_mm);
    readIfExists(root, "min_valid_scan_count", config.min_valid_scan_count);
    readIfExists(root, "max_frame_jump_mm", config.max_frame_jump_mm);
    readIfExists(root, "min_edge_length_ratio", config.min_edge_length_ratio);

    int fallback_to_abs_gradient = config.fallback_to_abs_gradient ? 1 : 0;
    readIfExists(root, "fallback_to_abs_gradient", fallback_to_abs_gradient);
    config.fallback_to_abs_gradient = fallback_to_abs_gradient != 0;

    int enable_measurement_csv = config.enable_measurement_csv ? 1 : 0;
    readIfExists(root, "enable_measurement_csv", enable_measurement_csv);
    config.enable_measurement_csv = enable_measurement_csv != 0;
    readIfExists(root, "measurement_csv_path", config.measurement_csv_path);
    readIfExists(root, "standard_true_mm", config.standard_true_mm);
    readIfExists(root, "residual_compensation_file", config.residual_compensation_file);

    int show_grid_guide = config.show_grid_guide ? 1 : 0;
    readIfExists(root, "show_grid_guide", show_grid_guide);
    config.show_grid_guide = show_grid_guide != 0;
    readIfExists(root, "grid_guide_position", config.grid_guide_position);

    int show_window = config.show_window ? 1 : 0;
    readIfExists(root, "show_window", show_window);
    config.show_window = show_window != 0;
    readIfExists(root, "display_max_width", config.display_max_width);
    readIfExists(root, "display_max_height", config.display_max_height);

    int strict_calibration = config.strict_calibration ? 1 : 0;
    readIfExists(root, "strict_calibration", strict_calibration);
    config.strict_calibration = strict_calibration != 0;

    validateConfig(config);

    const std::filesystem::path base_dir = std::filesystem::absolute(std::filesystem::path(yaml_path)).parent_path();
    auto resolvePath = [&](std::string& value) {
        if (value.empty()) {
            return;
        }
        std::filesystem::path p(value);
        if (p.is_absolute()) {
            return;
        }
        std::filesystem::path rebased = (base_dir / p).lexically_normal();
        if (std::filesystem::exists(rebased)) {
            value = rebased.string();
        }
    };

    resolvePath(config.onnx_model_path);
    resolvePath(config.class_names_path);
    resolvePath(config.calibration_file);
    resolvePath(config.measurement_csv_path);
    resolvePath(config.residual_compensation_file);

    return config;
}
