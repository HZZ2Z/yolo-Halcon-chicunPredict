#pragma once

#include "types.hpp"

#include <functional>
#include <opencv2/opencv.hpp>

struct PipelineCallbacks {
    std::function<void(const cv::Mat& image,
                       const OBBResult& tracked,
                       const MeasurementResult& measurement)> on_frame;
    std::function<bool()> should_stop;
};
