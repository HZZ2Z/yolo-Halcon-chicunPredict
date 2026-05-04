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
    int multi_scan_count = 7;
    int edge_refine_half_window = 4;
    float edge_power_gamma = 2.0f;
    float huber_delta_mm = 0.05f;
    bool estimate_measurement_uncertainty = true;
    float max_sigma_mm = 0.10f;
    int min_valid_scan_count = 5;
    float max_frame_jump_mm = 2.00f;
    float min_edge_length_ratio = 0.82f;
    bool fallback_to_abs_gradient = true;

    bool enable_measurement_csv = false;
    std::string measurement_csv_path = "measurements.csv";
    float standard_true_mm = -1.0f;
    std::string residual_compensation_file;

    bool show_grid_guide = false;
    std::string grid_guide_position;

    bool show_window = true;
    int display_max_width = 1280;
    int display_max_height = 760;
    bool strict_calibration = true;
};

AppConfig LoadConfig(const std::string& yaml_path);
