#pragma once

#include <cstdint>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

struct FrameData {
    uint64_t frame_id = 0;
    cv::Mat image;
    double timestamp = 0.0;
};

struct OBBResult {
    cv::RotatedRect rrect;
    int class_id = -1;
    float confidence = 0.0f;
};

struct MeasurementResult {
    uint64_t frame_id = 0;
    bool valid = false;
    float pixel_distance = 0.0f;
    float world_distance_mm = -1.0f;
    float raw_world_distance_mm = -1.0f;
    float world_sigma_mm = -1.0f;
    cv::Point2f left_edge_px;
    cv::Point2f right_edge_px;
    cv::Matx22f left_edge_cov_px = cv::Matx22f::zeros();
    cv::Matx22f right_edge_cov_px = cv::Matx22f::zeros();
    int valid_scan_count = 0;
    std::vector<cv::Point2f> left_edge_samples_px;
    std::vector<cv::Point2f> right_edge_samples_px;
    std::vector<cv::Matx22f> left_edge_sample_covs_px;
    std::vector<cv::Matx22f> right_edge_sample_covs_px;
    std::vector<float> scan_quality_samples;
    float measurement_quality = 0.0f;
    bool quality_ok = true;
    std::string quality_reason = "OK";
};

inline float deg2rad(float deg) {
    return deg * static_cast<float>(CV_PI / 180.0);
}

inline float rad2deg(float rad) {
    return rad * static_cast<float>(180.0 / CV_PI);
}
