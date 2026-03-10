#include "config.hpp"

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

    int show_window = config.show_window ? 1 : 0;
    readIfExists(root, "show_window", show_window);
    config.show_window = show_window != 0;

    int enable_diag_logs = config.enable_diag_logs ? 1 : 0;
    readIfExists(root, "enable_diag_logs", enable_diag_logs);
    config.enable_diag_logs = enable_diag_logs != 0;

    int strict_calibration = config.strict_calibration ? 1 : 0;
    readIfExists(root, "strict_calibration", strict_calibration);
    config.strict_calibration = strict_calibration != 0;

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

    return config;
}
