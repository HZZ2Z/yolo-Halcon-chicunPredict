#include "tracker_ekf.hpp"

ObbTracker::ObbTracker() {
    kf_ = cv::KalmanFilter(6, 3, 0, CV_32F);
    kf_.transitionMatrix = (cv::Mat_<float>(6, 6) << 1, 0, 0, 1, 0, 0,
                             0, 1, 0, 0, 1, 0,
                             0, 0, 1, 0, 0, 1,
                             0, 0, 0, 1, 0, 0,
                             0, 0, 0, 0, 1, 0,
                             0, 0, 0, 0, 0, 1);
    kf_.measurementMatrix = (cv::Mat_<float>(3, 6) << 1, 0, 0, 0, 0, 0,
                              0, 1, 0, 0, 0, 0,
                              0, 0, 1, 0, 0, 0);
    setIdentity(kf_.processNoiseCov, cv::Scalar::all(1e-3));
    setIdentity(kf_.measurementNoiseCov, cv::Scalar::all(2e-2));
    setIdentity(kf_.errorCovPost, cv::Scalar::all(1));
}

void ObbTracker::reset() {
    initialized_ = false;
}

OBBResult ObbTracker::update(const std::optional<OBBResult>& obs) {
    OBBResult result;

    if (!initialized_ && !obs.has_value()) {
        result.class_id = -1;
        result.confidence = 0.0f;
        result.rrect = cv::RotatedRect(cv::Point2f(0, 0), cv::Size2f(0, 0), 0.0f);
        return result;
    }

    cv::Mat state = kf_.predict();

    if (obs.has_value()) {
        const auto& o = obs.value();
        cv::Mat measurement = (cv::Mat_<float>(3, 1) << o.rrect.center.x, o.rrect.center.y, o.rrect.angle);

        if (!initialized_) {
            kf_.statePost = (cv::Mat_<float>(6, 1) << o.rrect.center.x, o.rrect.center.y, o.rrect.angle, 0, 0, 0);
            initialized_ = true;
            state = kf_.statePost;
        } else {
            state = kf_.correct(measurement);
        }
        last_w_ = o.rrect.size.width;
        last_h_ = o.rrect.size.height;
        last_class_ = o.class_id;
        last_conf_ = o.confidence;
    }

    result.rrect.center = cv::Point2f(state.at<float>(0), state.at<float>(1));
    result.rrect.angle = state.at<float>(2);
    result.rrect.size = cv::Size2f(last_w_ > 1e-3f ? last_w_ : 100.0f, last_h_ > 1e-3f ? last_h_ : 30.0f);
    result.class_id = last_class_;
    result.confidence = last_conf_;
    return result;
}
