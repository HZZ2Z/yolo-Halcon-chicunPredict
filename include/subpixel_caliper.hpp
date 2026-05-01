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
                    bool measure_long_edge,
                    int multi_scan_count,
                    int edge_refine_half_window,
                    float edge_power_gamma);
    MeasurementResult measure(const FrameData& frame,
                              const OBBResult& obb,
                              const cv::Matx33f* pose_covariance = nullptr) const;

private:
    struct EdgeEstimate {
        bool valid = false;
        float u = 0.0f;
        float variance_u = 1.0f;
        float strength = 0.0f;
    };

    float bilinearAt(const cv::Mat& gray, float x, float y) const;
    float gaussianKernel1D(float x, float sigma) const;
    float subpixelPeak(const std::vector<float>& signal, int idx) const;
    EdgeEstimate refineEdge(const std::vector<float>& abs_grad,
                            int start,
                            int end,
                            float scan_length) const;

    float length_;
    float half_width_;
    float sigma_;
    float search_scale_;
    bool use_obb_adaptive_;
    bool measure_long_edge_;
    int multi_scan_count_;
    int edge_refine_half_window_;
    float edge_power_gamma_;
};
