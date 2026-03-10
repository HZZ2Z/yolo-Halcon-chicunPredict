#pragma once

#include <string>

struct AppConfig {
    std::string input_source;
    std::string onnx_model_path;
    std::string class_names_path;
    std::string calibration_file;

    float focal_length_mm = 8.0f;
    float pixel_size_um = 4.0f;

    int frame_width = 1280;
    int frame_height = 1024;
    int max_frames = 300;
    int queue_size = 30;
    int infer_interval = 1;
    int measure_interval = 1;
    int render_interval = 1;

    float conf_threshold = 0.35f;
    float nms_threshold = 0.3f;

    float caliper_length = 80.0f;
    float caliper_half_width = 8.0f;
    float gaussian_sigma = 1.0f;
    float caliper_search_scale = 1.25f;
    bool use_obb_adaptive_caliper = true;
    bool measure_long_edge = true;

    bool show_window = true;
    bool enable_diag_logs = false;
    bool strict_calibration = true;
};

AppConfig LoadConfig(const std::string& yaml_path);
