#include "subpixel_caliper.hpp"

#include "measurement_uncertainty.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {

float pointDot(const cv::Point2f& a, const cv::Point2f& b) {
    return a.x * b.x + a.y * b.y;
}

cv::Matx22f outerProduct(const cv::Point2f& v, float scale) {
    return cv::Matx22f(v.x * v.x * scale, v.x * v.y * scale,
                       v.y * v.x * scale, v.y * v.y * scale);
}

cv::Matx33f poseCovarianceRadians(const cv::Matx33f& cov_deg) {
    const float k = static_cast<float>(CV_PI / 180.0);
    const float scale[3] = {1.0f, 1.0f, k};
    cv::Matx33f out = cv::Matx33f::zeros();
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            out(r, c) = cov_deg(r, c) * scale[r] * scale[c];
        }
    }
    return out;
}

cv::Matx22f pointCovariance(const cv::Point2f& tangent,
                            const cv::Point2f& normal,
                            float scan_offset,
                            float edge_u,
                            float variance_u,
                            const cv::Matx33f& pose_cov_deg) {
    const cv::Matx33f pose_cov = poseCovarianceRadians(pose_cov_deg);
    const cv::Point2f theta_col = tangent * (-scan_offset) + normal * edge_u;

    const float jx[3] = {1.0f, 0.0f, theta_col.x};
    const float jy[3] = {0.0f, 1.0f, theta_col.y};

    cv::Matx22f cov = outerProduct(tangent, std::max(0.01f, variance_u));
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            cov(0, 0) += jx[r] * pose_cov(r, c) * jx[c];
            cov(0, 1) += jx[r] * pose_cov(r, c) * jy[c];
            cov(1, 0) += jy[r] * pose_cov(r, c) * jx[c];
            cov(1, 1) += jy[r] * pose_cov(r, c) * jy[c];
        }
    }
    return cov;
}

float robustMean1D(const std::vector<float>& values, float delta) {
    std::vector<double> v;
    std::vector<double> vars;
    v.reserve(values.size());
    vars.reserve(values.size());
    for (float value : values) {
        v.push_back(value);
        vars.push_back(1.0);
    }
    const RobustMeanResult mean = HuberWeightedMean(v, vars, std::max(1e-3f, delta), 5);
    return static_cast<float>(mean.mean);
}

float meanRange(const std::vector<float>& values, int start, int end) {
    start = std::clamp(start, 0, static_cast<int>(values.size()) - 1);
    end = std::clamp(end, start, static_cast<int>(values.size()) - 1);
    double sum = 0.0;
    for (int i = start; i <= end; ++i) {
        sum += values[i];
    }
    return static_cast<float>(sum / std::max(1, end - start + 1));
}

float medianOf(std::vector<float> values) {
    if (values.empty()) {
        return 0.0f;
    }
    std::sort(values.begin(), values.end());
    const size_t mid = values.size() / 2;
    if (values.size() % 2 == 1) {
        return values[mid];
    }
    return 0.5f * (values[mid - 1] + values[mid]);
}

}  // namespace

SubpixelCaliper::SubpixelCaliper(float length,
                                 float half_width,
                                 float sigma,
                                 float search_scale,
                                 bool use_obb_adaptive,
                                 bool measure_long_edge,
                                 int multi_scan_count,
                                 int edge_refine_half_window,
                                 float edge_power_gamma,
                                 float min_edge_length_ratio,
                                 bool fallback_to_abs_gradient)
    : length_(length),
      half_width_(half_width),
      sigma_(sigma),
      search_scale_(search_scale),
      use_obb_adaptive_(use_obb_adaptive),
      measure_long_edge_(measure_long_edge),
      multi_scan_count_(std::max(1, multi_scan_count)),
      edge_refine_half_window_(std::max(1, edge_refine_half_window)),
      edge_power_gamma_(std::max(0.25f, edge_power_gamma)),
      min_edge_length_ratio_(std::clamp(min_edge_length_ratio, 0.1f, 1.0f)),
      fallback_to_abs_gradient_(fallback_to_abs_gradient) {}

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

SubpixelCaliper::EdgeEstimate SubpixelCaliper::refineEdge(const std::vector<float>& response,
                                                          int start,
                                                          int end,
                                                          float scan_length) const {
    EdgeEstimate edge;
    if (end <= start || response.size() < 3) {
        return edge;
    }

    int peak_idx = start;
    float peak = response[start];
    double mean_response = 0.0;
    for (int i = start; i <= end; ++i) {
        mean_response += std::max(0.0f, response[i]);
        if (response[i] > peak) {
            peak = response[i];
            peak_idx = i;
        }
    }
    mean_response /= std::max(1, end - start + 1);
    if (peak <= 0.0f) {
        return edge;
    }

    const int first = std::max(1, peak_idx - edge_refine_half_window_);
    const int last = std::min(static_cast<int>(response.size()) - 2, peak_idx + edge_refine_half_window_);
    double wsum = 0.0;
    double usum = 0.0;
    for (int i = first; i <= last; ++i) {
        const double w = std::pow(std::max(0.0f, response[i]), edge_power_gamma_);
        wsum += w;
        usum += w * static_cast<double>(i);
    }

    double refined_idx = subpixelPeak(response, peak_idx);
    if (wsum > 1e-9) {
        refined_idx = usum / wsum;
    }

    double variance_idx = 0.25;
    if (wsum > 1e-9) {
        variance_idx = 0.0;
        for (int i = first; i <= last; ++i) {
            const double w = std::pow(std::max(0.0f, response[i]), edge_power_gamma_);
            const double d = static_cast<double>(i) - refined_idx;
            variance_idx += w * d * d;
        }
        variance_idx /= wsum;
    }

    const double px_per_sample = scan_length / std::max(1, static_cast<int>(response.size()) - 1);
    edge.valid = true;
    edge.u = static_cast<float>((refined_idx / (response.size() - 1) - 0.5) * scan_length);
    edge.variance_u = static_cast<float>(std::max(0.02, variance_idx * px_per_sample * px_per_sample));
    edge.strength = peak;
    edge.peak_ratio = static_cast<float>(peak / std::max(1e-3, mean_response));
    const float variance_quality = 1.0f / (1.0f + edge.variance_u);
    const float ratio_quality = std::clamp((edge.peak_ratio - 1.0f) / 4.0f, 0.0f, 1.0f);
    const float strength_quality = std::clamp(edge.strength / 0.8f, 0.0f, 1.0f);
    edge.quality = std::clamp(0.45f * strength_quality + 0.35f * ratio_quality +
                                  0.20f * variance_quality,
                              0.0f,
                              1.0f);
    return edge;
}

MeasurementResult SubpixelCaliper::measure(const FrameData& frame,
                                           const OBBResult& obb,
                                           const cv::Matx33f* pose_covariance) const {
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
    if (use_obb_adaptive_ || scan_length <= 0.0f) {
        scan_length = target_edge_len * std::max(1.05f, search_scale_);
    }
    scan_length = std::max(10.0f, scan_length);

    float band_half_width = half_width_;
    if (use_obb_adaptive_ && half_width_ <= 0.0f) {
        band_half_width = std::max(1.0f, orth_edge_len * 0.05f);
    }
    band_half_width = std::clamp(band_half_width, 1.0f, std::max(1.0f, orth_edge_len * 0.08f));

    const int samples = static_cast<int>(std::max(10.0f, scan_length));
    const int smooth_radius = std::max(1, static_cast<int>(std::round(2.0f * std::max(0.5f, sigma_))));
    const int scan_count = std::max(1, multi_scan_count_);
    const float usable_half_span = std::max(0.0f, orth_edge_len * 0.38f);
    const cv::Matx33f default_pose_cov(0.25f, 0.0f, 0.0f,
                                       0.0f, 0.25f, 0.0f,
                                       0.0f, 0.0f, 0.25f);
    const cv::Matx33f pose_cov = pose_covariance ? *pose_covariance : default_pose_cov;

    std::vector<float> left_us;
    std::vector<float> right_us;
    std::vector<float> measured_lengths;

    for (int scan = 0; scan < scan_count; ++scan) {
        const float scan_offset =
            scan_count == 1 ? 0.0f
                            : (-usable_half_span +
                               (2.0f * usable_half_span * scan) / static_cast<float>(scan_count - 1));

        std::vector<float> profile(samples, 0.0f);
        for (int i = 0; i < samples; ++i) {
            const float u = (static_cast<float>(i) / (samples - 1) - 0.5f) * scan_length;
            const cv::Point2f p = obb.rrect.center + normal * scan_offset + tangent * u;

            float acc = 0.0f;
            int cnt = 0;
            const int band = static_cast<int>(std::round(band_half_width));
            for (int k = -band; k <= band; ++k) {
                const cv::Point2f q = p + normal * static_cast<float>(k);
                acc += bilinearAt(gray, q.x, q.y);
                ++cnt;
            }
            profile[i] = acc / std::max(1, cnt);
        }

        const int edge_count = std::clamp(samples / 12, 4, 30);
        const int center_half = std::clamp(samples / 16, 4, 30);
        const float edge_mean =
            0.5f * (meanRange(profile, 0, edge_count - 1) +
                    meanRange(profile, samples - edge_count, samples - 1));
        const float center_mean =
            meanRange(profile, samples / 2 - center_half, samples / 2 + center_half);
        const bool dark_object = center_mean < edge_mean;
        const float left_polarity = dark_object ? -1.0f : 1.0f;
        const float right_polarity = -left_polarity;

        std::vector<float> smooth_profile(samples, 0.0f);
        for (int i = 0; i < samples; ++i) {
            float wsum = 0.0f;
            float vsum = 0.0f;
            for (int k = -smooth_radius; k <= smooth_radius; ++k) {
                const int j = std::clamp(i + k, 0, samples - 1);
                const float w = gaussianKernel1D(static_cast<float>(k), std::max(0.5f, sigma_));
                wsum += w;
                vsum += w * profile[j];
            }
            smooth_profile[i] = vsum / std::max(1e-6f, wsum);
        }

        std::vector<float> grad(samples, 0.0f);
        std::vector<float> abs_grad(samples, 0.0f);
        std::vector<float> left_response(samples, 0.0f);
        std::vector<float> right_response(samples, 0.0f);
        for (int i = 1; i < samples - 1; ++i) {
            grad[i] = (smooth_profile[i + 1] - smooth_profile[i - 1]) * 0.5f;
            abs_grad[i] = std::abs(grad[i]);
            left_response[i] = std::max(0.0f, left_polarity * grad[i]);
            right_response[i] = std::max(0.0f, right_polarity * grad[i]);
        }

        const int mid = samples / 2;
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
            static_cast<int>(std::round(0.20f * target_edge_len * (samples - 1) /
                                        std::max(1e-3f, scan_length))));

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
            continue;
        }

        float mean_response = 0.0f;
        float mean_abs_grad = 0.0f;
        for (int i = left_start; i <= right_end; ++i) {
            mean_response += left_response[i] + right_response[i];
            mean_abs_grad += abs_grad[i];
        }
        mean_response /= std::max(1, 2 * (right_end - left_start + 1));
        mean_abs_grad /= std::max(1, right_end - left_start + 1);

        EdgeEstimate left = refineEdge(left_response, left_start, left_end, scan_length);
        EdgeEstimate right = refineEdge(right_response, right_start, right_end, scan_length);
        bool use_polar_edges = left.valid && right.valid && left.u < right.u &&
                               left.strength >= std::max(1.2f, 1.4f * mean_response) &&
                               right.strength >= std::max(1.2f, 1.4f * mean_response);

        const float min_measured_len = std::max(5.0f, target_edge_len * min_edge_length_ratio_);
        if (use_polar_edges && std::abs(right.u - left.u) < min_measured_len) {
            use_polar_edges = false;
        }

        if (!use_polar_edges && fallback_to_abs_gradient_) {
            EdgeEstimate abs_left = refineEdge(abs_grad, left_start, left_end, scan_length);
            EdgeEstimate abs_right = refineEdge(abs_grad, right_start, right_end, scan_length);
            const bool abs_ok = abs_left.valid && abs_right.valid && abs_left.u < abs_right.u &&
                                abs_left.strength >= std::max(1.2f, 1.4f * mean_abs_grad) &&
                                abs_right.strength >= std::max(1.2f, 1.4f * mean_abs_grad);
            if (abs_ok && std::abs(abs_right.u - abs_left.u) >= min_measured_len) {
                left = abs_left;
                right = abs_right;
                use_polar_edges = true;
            }
        }

        if (!use_polar_edges) {
            continue;
        }

        const float measured_len = std::abs(right.u - left.u);
        const float length_consistency =
            std::clamp(1.0f - std::abs(measured_len - target_edge_len) /
                                  std::max(1.0f, target_edge_len * 0.20f),
                       0.0f,
                       1.0f);
        const float scan_quality =
            std::clamp(std::sqrt(std::max(0.0f, left.quality * right.quality)) *
                           (0.50f + 0.50f * length_consistency),
                       0.0f,
                       1.0f);
        if (scan_quality < 0.20f) {
            continue;
        }

        const cv::Point2f left_px = obb.rrect.center + normal * scan_offset + tangent * left.u;
        const cv::Point2f right_px = obb.rrect.center + normal * scan_offset + tangent * right.u;
        const cv::Matx22f left_cov =
            pointCovariance(tangent, normal, scan_offset, left.u, left.variance_u, pose_cov);
        const cv::Matx22f right_cov =
            pointCovariance(tangent, normal, scan_offset, right.u, right.variance_u, pose_cov);

        mr.left_edge_samples_px.push_back(left_px);
        mr.right_edge_samples_px.push_back(right_px);
        mr.left_edge_sample_covs_px.push_back(left_cov);
        mr.right_edge_sample_covs_px.push_back(right_cov);
        mr.scan_quality_samples.push_back(scan_quality);
        left_us.push_back(left.u);
        right_us.push_back(right.u);
        measured_lengths.push_back(measured_len);
    }

    if (!measured_lengths.empty()) {
        const float median_len = medianOf(measured_lengths);
        const float tolerance = std::max(2.0f, target_edge_len * 0.035f);
        std::vector<cv::Point2f> kept_left_px;
        std::vector<cv::Point2f> kept_right_px;
        std::vector<cv::Matx22f> kept_left_cov;
        std::vector<cv::Matx22f> kept_right_cov;
        std::vector<float> kept_left_us;
        std::vector<float> kept_right_us;
        std::vector<float> kept_quality;
        for (size_t i = 0; i < measured_lengths.size(); ++i) {
            if (std::abs(measured_lengths[i] - median_len) > tolerance) {
                continue;
            }
            kept_left_px.push_back(mr.left_edge_samples_px[i]);
            kept_right_px.push_back(mr.right_edge_samples_px[i]);
            kept_left_cov.push_back(mr.left_edge_sample_covs_px[i]);
            kept_right_cov.push_back(mr.right_edge_sample_covs_px[i]);
            kept_left_us.push_back(left_us[i]);
            kept_right_us.push_back(right_us[i]);
            kept_quality.push_back(mr.scan_quality_samples[i]);
        }
        mr.left_edge_samples_px = std::move(kept_left_px);
        mr.right_edge_samples_px = std::move(kept_right_px);
        mr.left_edge_sample_covs_px = std::move(kept_left_cov);
        mr.right_edge_sample_covs_px = std::move(kept_right_cov);
        mr.scan_quality_samples = std::move(kept_quality);
        left_us = std::move(kept_left_us);
        right_us = std::move(kept_right_us);
    }

    mr.valid_scan_count = static_cast<int>(mr.left_edge_samples_px.size());
    const int min_required = scan_count >= 5 ? 3 : 1;
    if (mr.valid_scan_count < min_required) {
        return mr;
    }

    const float left_u = robustMean1D(left_us, std::max(1.0f, target_edge_len * 0.04f));
    const float right_u = robustMean1D(right_us, std::max(1.0f, target_edge_len * 0.04f));
    if (left_u >= right_u) {
        return MeasurementResult{};
    }

    mr.left_edge_px = obb.rrect.center + tangent * left_u;
    mr.right_edge_px = obb.rrect.center + tangent * right_u;
    mr.pixel_distance = std::abs(right_u - left_u);

    cv::Matx22f left_cov = cv::Matx22f::zeros();
    cv::Matx22f right_cov = cv::Matx22f::zeros();
    for (int i = 0; i < mr.valid_scan_count; ++i) {
        left_cov += mr.left_edge_sample_covs_px[i];
        right_cov += mr.right_edge_sample_covs_px[i];
    }
    mr.left_edge_cov_px = left_cov * (1.0f / std::max(1, mr.valid_scan_count * mr.valid_scan_count));
    mr.right_edge_cov_px = right_cov * (1.0f / std::max(1, mr.valid_scan_count * mr.valid_scan_count));
    if (!mr.scan_quality_samples.empty()) {
        mr.measurement_quality =
            std::accumulate(mr.scan_quality_samples.begin(), mr.scan_quality_samples.end(), 0.0f) /
            static_cast<float>(mr.scan_quality_samples.size());
    }
    mr.valid = true;
    return mr;
}
