#pragma once

#include <cstdint>
#include <opencv2/opencv.hpp>
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
    float world_distance_mm = 0.0f;
    cv::Point2f left_edge_px;
    cv::Point2f right_edge_px;
};

inline float deg2rad(float deg) {
    return deg * static_cast<float>(CV_PI / 180.0);
}

inline float rad2deg(float rad) {
    return rad * static_cast<float>(180.0 / CV_PI);
}
