#pragma once

#include <opencv2/opencv.hpp>

#include <string>
#include <vector>

class SpatialErrorCompensator {
public:
    bool load(const std::string& path);
    bool ready() const;
    float correction(float x, float y) const;
    float apply(float measured_mm, float x, float y) const;

private:
    struct Sample {
        cv::Point2f position;
        float correction_mm = 0.0f;
    };

    std::vector<Sample> samples_;
};
