#include "measurement_uncertainty.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

double finiteVariance(double v) {
    if (!std::isfinite(v) || v <= 1e-12) {
        return 1.0;
    }
    return v;
}

}  // namespace

cv::Matx22d NumericalPixelJacobianWorld(
    const std::function<cv::Point2d(const cv::Point2d&)>& pixel_to_world,
    const cv::Point2d& px,
    double eps_px) {
    eps_px = std::max(1e-3, eps_px);
    const cv::Point2d wx_p = pixel_to_world(cv::Point2d(px.x + eps_px, px.y));
    const cv::Point2d wx_m = pixel_to_world(cv::Point2d(px.x - eps_px, px.y));
    const cv::Point2d wy_p = pixel_to_world(cv::Point2d(px.x, px.y + eps_px));
    const cv::Point2d wy_m = pixel_to_world(cv::Point2d(px.x, px.y - eps_px));

    return cv::Matx22d((wx_p.x - wx_m.x) / (2.0 * eps_px),
                       (wy_p.x - wy_m.x) / (2.0 * eps_px),
                       (wx_p.y - wx_m.y) / (2.0 * eps_px),
                       (wy_p.y - wy_m.y) / (2.0 * eps_px));
}

cv::Matx22d NumericalPixelJacobianWorld(const CalibrationMapper& calibration,
                                        const cv::Point2d& px,
                                        double eps_px) {
    return NumericalPixelJacobianWorld(
        [&calibration](const cv::Point2d& p) {
            const cv::Point2f w = calibration.pixelToWorld(
                cv::Point2f(static_cast<float>(p.x), static_cast<float>(p.y)));
            return cv::Point2d(w.x, w.y);
        },
        px,
        eps_px);
}

double PropagateDistanceVariance(const cv::Point2d& w0,
                                 const cv::Matx22d& cov0,
                                 const cv::Point2d& w1,
                                 const cv::Matx22d& cov1) {
    const cv::Point2d delta = w1 - w0;
    const double dist = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (!std::isfinite(dist) || dist < 1e-12) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const cv::Vec2d j0(-delta.x / dist, -delta.y / dist);
    const cv::Vec2d j1(delta.x / dist, delta.y / dist);
    const double v0 = j0.dot(cv::Vec2d(cov0(0, 0) * j0[0] + cov0(0, 1) * j0[1],
                                       cov0(1, 0) * j0[0] + cov0(1, 1) * j0[1]));
    const double v1 = j1.dot(cv::Vec2d(cov1(0, 0) * j1[0] + cov1(0, 1) * j1[1],
                                       cov1(1, 0) * j1[0] + cov1(1, 1) * j1[1]));
    return std::max(0.0, v0 + v1);
}

double ProjectVarianceAlongNormal(const cv::Matx22d& cov_w, const cv::Vec2d& n_unit) {
    const cv::Vec2d tmp(cov_w(0, 0) * n_unit[0] + cov_w(0, 1) * n_unit[1],
                        cov_w(1, 0) * n_unit[0] + cov_w(1, 1) * n_unit[1]);
    return std::max(0.0, n_unit.dot(tmp));
}

RobustMeanResult HuberWeightedMean(const std::vector<double>& values,
                                   const std::vector<double>& variances,
                                   double huber_delta,
                                   int iterations) {
    RobustMeanResult result;
    if (values.empty()) {
        return result;
    }

    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    double mean = sorted[sorted.size() / 2];
    huber_delta = std::max(1e-9, huber_delta);
    iterations = std::max(1, iterations);

    for (int iter = 0; iter < iterations; ++iter) {
        double wsum = 0.0;
        double vsum = 0.0;
        for (size_t i = 0; i < values.size(); ++i) {
            const double variance = finiteVariance(i < variances.size() ? variances[i] : 1.0);
            const double residual = values[i] - mean;
            const double abs_residual = std::abs(residual);
            const double huber_weight = abs_residual <= huber_delta ? 1.0 : huber_delta / abs_residual;
            const double weight = huber_weight / variance;
            wsum += weight;
            vsum += weight * values[i];
        }
        if (wsum <= 1e-12) {
            break;
        }
        mean = vsum / wsum;
    }

    double final_wsum = 0.0;
    for (size_t i = 0; i < values.size(); ++i) {
        const double variance = finiteVariance(i < variances.size() ? variances[i] : 1.0);
        const double residual = values[i] - mean;
        const double abs_residual = std::abs(residual);
        const double huber_weight = abs_residual <= huber_delta ? 1.0 : huber_delta / abs_residual;
        final_wsum += huber_weight / variance;
    }

    result.mean = mean;
    result.variance = final_wsum > 1e-12 ? 1.0 / final_wsum : -1.0;
    result.used = static_cast<int>(values.size());
    return result;
}
