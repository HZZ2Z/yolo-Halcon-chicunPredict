#pragma once

#include "types.hpp"

#include <opencv2/opencv.hpp>
#include <optional>

class ObbTracker {
public:
    ObbTracker();
    void reset();
    OBBResult update(const std::optional<OBBResult>& obs);
    cv::Matx33f poseCovariance() const;

private:
    cv::KalmanFilter kf_;
    bool initialized_ = false;
    float last_w_ = 0.0f;
    float last_h_ = 0.0f;
    int last_class_ = -1;
    float last_conf_ = 0.0f;
};
