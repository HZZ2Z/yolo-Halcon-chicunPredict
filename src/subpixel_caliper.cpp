#include "subpixel_caliper.hpp"

#include <algorithm>
#include <cmath>

SubpixelCaliper::SubpixelCaliper(float length,
                                                                 float half_width,
                                                                 float sigma,
                                                                 float search_scale,
                                                                 bool use_obb_adaptive,
                                                                 bool measure_long_edge)
        : length_(length),
            half_width_(half_width),
            sigma_(sigma),
            search_scale_(search_scale),
            use_obb_adaptive_(use_obb_adaptive),
            measure_long_edge_(measure_long_edge) {}

float SubpixelCaliper::bilinearAt(const cv::Mat& gray, float x, float y) const {
    x = std::clamp(x, 0.0f, static_cast<float>(gray.cols - 1));
    y = std::clamp(y, 0.0f, static_cast<float>(gray.rows - 1));

    int x0 = static_cast<int>(x);
    int y0 = static_cast<int>(y);
    int x1 = std::min(x0 + 1, gray.cols - 1);
    int y1 = std::min(y0 + 1, gray.rows - 1);

    float dx = x - x0;
    float dy = y - y0;

    float i00 = gray.at<uchar>(y0, x0);
    float i10 = gray.at<uchar>(y0, x1);
    float i01 = gray.at<uchar>(y1, x0);
    float i11 = gray.at<uchar>(y1, x1);

    float i0 = i00 * (1 - dx) + i10 * dx;
    float i1 = i01 * (1 - dx) + i11 * dx;
    return i0 * (1 - dy) + i1 * dy;
}

float SubpixelCaliper::gaussianKernel1D(float x, float sigma) const {
    float s2 = sigma * sigma;
    return std::exp(-(x * x) / (2.0f * s2));
}

float SubpixelCaliper::subpixelPeak(const std::vector<float>& signal, int idx) const {
    if (idx <= 0 || idx >= static_cast<int>(signal.size()) - 1) {
        return static_cast<float>(idx);
    }
    float ym1 = signal[idx - 1];
    float y0 = signal[idx];
    float yp1 = signal[idx + 1];
    float denom = 2.0f * (ym1 - 2.0f * y0 + yp1);
    if (std::abs(denom) < 1e-6f) {
        return static_cast<float>(idx);
    }
    float t = (ym1 - yp1) / denom;
    t = std::clamp(t, -0.5f, 0.5f);
    return static_cast<float>(idx) + t;
}

MeasurementResult SubpixelCaliper::measure(const FrameData& frame, const OBBResult& obb) const {
    MeasurementResult mr;
    mr.frame_id = frame.frame_id;
    if (frame.image.empty()) {
        return mr;
    }
    if (obb.rrect.size.width < 2.0f || obb.rrect.size.height < 2.0f || obb.confidence <= 0.0f) {
        return mr;
    }

    cv::Mat gray;
    if (frame.image.channels() == 3) {
        cv::cvtColor(frame.image, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = frame.image;
    }

    cv::Point2f corners[4];
    obb.rrect.points(corners);

    const cv::Point2f edge01 = corners[1] - corners[0];
    const cv::Point2f edge12 = corners[2] - corners[1];
    const float len01 = std::max(1e-6f, static_cast<float>(cv::norm(edge01)));
    const float len12 = std::max(1e-6f, static_cast<float>(cv::norm(edge12)));

    const bool edge01_is_long = len01 >= len12;
    cv::Point2f long_dir = edge01_is_long ? (edge01 * (1.0f / len01)) : (edge12 * (1.0f / len12));
    cv::Point2f short_dir = edge01_is_long ? (edge12 * (1.0f / len12)) : (edge01 * (1.0f / len01));
    const float long_len = std::max(len01, len12);
    const float short_len = std::min(len01, len12);

    const cv::Point2f tangent = measure_long_edge_ ? long_dir : short_dir;
    const cv::Point2f normal(-tangent.y, tangent.x);

    const float target_edge_len = measure_long_edge_ ? long_len : short_len;
    const float orth_edge_len = measure_long_edge_ ? short_len : long_len;

    float scan_length = length_;
    if (use_obb_adaptive_) {
        scan_length = target_edge_len * std::max(1.05f, search_scale_);
    }
    scan_length = std::max(10.0f, scan_length);

    float band_half_width = half_width_;
    if (use_obb_adaptive_ && half_width_ <= 0.0f) {
        band_half_width = std::max(2.0f, orth_edge_len * 0.15f);
    }
    band_half_width = std::max(1.0f, band_half_width);

    int samples = static_cast<int>(std::max(10.0f, scan_length));
    std::vector<float> profile(samples, 0.0f);

    for (int i = 0; i < samples; ++i) {
        float u = (static_cast<float>(i) / (samples - 1) - 0.5f) * scan_length;
        cv::Point2f p = obb.rrect.center + tangent * u;

        float acc = 0.0f;
        int cnt = 0;
        for (int k = -static_cast<int>(band_half_width); k <= static_cast<int>(band_half_width); ++k) {
            cv::Point2f q = p + normal * static_cast<float>(k);
            acc += bilinearAt(gray, q.x, q.y);
            ++cnt;
        }
        profile[i] = acc / std::max(1, cnt);
    }

    int radius = std::max(1, static_cast<int>(std::round(2.0f * std::max(0.5f, sigma_))));
    std::vector<float> smooth_profile(samples, 0.0f);
    for (int i = 0; i < samples; ++i) {
        float wsum = 0.0f;
        float vsum = 0.0f;
        for (int k = -radius; k <= radius; ++k) {
            int j = std::clamp(i + k, 0, samples - 1);
            float w = gaussianKernel1D(static_cast<float>(k), std::max(0.5f, sigma_));
            wsum += w;
            vsum += w * profile[j];
        }
        smooth_profile[i] = vsum / std::max(1e-6f, wsum);
    }

    std::vector<float> grad(samples, 0.0f);
    for (int i = 1; i < samples - 1; ++i) {
        grad[i] = (smooth_profile[i + 1] - smooth_profile[i - 1]) * 0.5f;
    }
    std::vector<float> abs_grad(samples, 0.0f);
    for (int i = 0; i < samples; ++i) {
        abs_grad[i] = std::abs(grad[i]);
    }

    int mid = samples / 2;

    const float expected_left_u = -0.5f * target_edge_len;
    const float expected_right_u = 0.5f * target_edge_len;
    const int expected_left_idx = std::clamp(
        static_cast<int>(std::round((expected_left_u / scan_length + 0.5f) * (samples - 1))),
        1,
        std::max(1, mid - 1));
    const int expected_right_idx = std::clamp(
        static_cast<int>(std::round((expected_right_u / scan_length + 0.5f) * (samples - 1))),
        std::min(samples - 2, mid + 1),
        samples - 2);

    const int expected_span = std::max(
        6,
        static_cast<int>(std::round(0.20f * target_edge_len * (samples - 1) / std::max(1e-3f, scan_length))));

    int left_start = std::max(1, expected_left_idx - expected_span);
    int left_end = std::min(std::max(1, mid - 1), expected_left_idx + expected_span);
    int right_start = std::max(std::min(samples - 2, mid + 1), expected_right_idx - expected_span);
    int right_end = std::min(samples - 2, expected_right_idx + expected_span);

    if (left_end <= left_start || right_end <= right_start) {
        left_start = std::max(1, static_cast<int>(samples * 0.1f));
        left_end = std::max(left_start + 1, mid - 1);
        right_start = std::min(samples - 2, mid + 1);
        right_end = std::min(samples - 2, static_cast<int>(samples * 0.9f));
    }

    if (left_end <= left_start || right_end <= right_start) {
        return mr;
    }

    int left_idx = left_start;
    int right_idx = right_start;
    float left_peak = abs_grad[left_idx];
    float right_peak = abs_grad[right_idx];

    for (int i = left_start; i <= left_end; ++i) {
        if (abs_grad[i] > left_peak) {
            left_peak = abs_grad[i];
            left_idx = i;
        }
    }
    for (int i = right_start; i <= right_end; ++i) {
        if (abs_grad[i] > right_peak) {
            right_peak = abs_grad[i];
            right_idx = i;
        }
    }

    float mean_abs_grad = 0.0f;
    for (int i = left_start; i <= right_end; ++i) {
        mean_abs_grad += abs_grad[i];
    }
    mean_abs_grad /= std::max(1, right_end - left_start + 1);
    if (left_peak < std::max(1.2f, 1.6f * mean_abs_grad) || right_peak < std::max(1.2f, 1.6f * mean_abs_grad)) {
        return mr;
    }

    if (left_idx >= right_idx) {
        return mr;
    }

    float left_sub = subpixelPeak(abs_grad, left_idx);
    float right_sub = subpixelPeak(abs_grad, right_idx);

    float left_u = (left_sub / (samples - 1) - 0.5f) * scan_length;
    float right_u = (right_sub / (samples - 1) - 0.5f) * scan_length;

    const float measured_len = std::abs(right_u - left_u);
    if (measured_len < std::max(5.0f, target_edge_len * 0.65f)) {
        return mr;
    }

    mr.left_edge_px = obb.rrect.center + tangent * left_u;
    mr.right_edge_px = obb.rrect.center + tangent * right_u;
    mr.pixel_distance = cv::norm(mr.right_edge_px - mr.left_edge_px);
    mr.valid = true;
    return mr;
}
