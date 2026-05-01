#pragma once

#include "calibration.hpp"

#include <functional>
#include <opencv2/opencv.hpp>
#include <vector>

struct RobustMeanResult {
    double mean = 0.0;
    double variance = -1.0;
    int used = 0;
};

cv::Matx22d NumericalPixelJacobianWorld(
    const std::function<cv::Point2d(const cv::Point2d&)>& pixel_to_world,
    const cv::Point2d& px,
    double eps_px = 0.5);

cv::Matx22d NumericalPixelJacobianWorld(const CalibrationMapper& calibration,
                                        const cv::Point2d& px,
                                        double eps_px = 0.5);

double PropagateDistanceVariance(const cv::Point2d& w0,
                                 const cv::Matx22d& cov0,
                                 const cv::Point2d& w1,
                                 const cv::Matx22d& cov1);

double ProjectVarianceAlongNormal(const cv::Matx22d& cov_w, const cv::Vec2d& n_unit);

RobustMeanResult HuberWeightedMean(const std::vector<double>& values,
                                   const std::vector<double>& variances,
                                   double huber_delta,
                                   int iterations = 5);
