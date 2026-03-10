#pragma once

#include "types.hpp"

#include <opencv2/opencv.hpp>

class SubpixelCaliper {
public:
    SubpixelCaliper(float length,
                    float half_width,
                    float sigma,
                    float search_scale,
                    bool use_obb_adaptive,
                    bool measure_long_edge);
    MeasurementResult measure(const FrameData& frame, const OBBResult& obb) const;

private:
    float bilinearAt(const cv::Mat& gray, float x, float y) const;
    float gaussianKernel1D(float x, float sigma) const;
    float subpixelPeak(const std::vector<float>& signal, int idx) const;

    float length_;
    float half_width_;
    float sigma_;
    float search_scale_;
    bool use_obb_adaptive_;
    bool measure_long_edge_;
};
