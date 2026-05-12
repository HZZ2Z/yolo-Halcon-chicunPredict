#include "app/frame_visualizer.hpp"

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace {

constexpr const char* kWindowName = "metal_metrology";

void prepareDisplayWindow(const AppConfig& cfg, const cv::Mat& image) {
    static bool initialized = false;
    static int last_width = 0;
    static int last_height = 0;

    if (image.empty()) {
        return;
    }

    const double scale_w = static_cast<double>(cfg.display_max_width) /
                           static_cast<double>(image.cols);
    const double scale_h = static_cast<double>(cfg.display_max_height) /
                           static_cast<double>(image.rows);
    const double scale = std::min(1.0, std::min(scale_w, scale_h));
    const int width = std::max(1, static_cast<int>(std::round(image.cols * scale)));
    const int height = std::max(1, static_cast<int>(std::round(image.rows * scale)));

    if (!initialized) {
        cv::namedWindow(kWindowName, cv::WINDOW_NORMAL | cv::WINDOW_KEEPRATIO);
        initialized = true;
    }
    if (width != last_width || height != last_height) {
        cv::resizeWindow(kWindowName, width, height);
        last_width = width;
        last_height = height;
    }
}

void putOutlinedText(cv::Mat& image,
                     const std::string& text,
                     const cv::Point& origin,
                     double scale,
                     const cv::Scalar& color,
                     int thickness = 2) {
    cv::putText(image, text, origin, cv::FONT_HERSHEY_SIMPLEX, scale,
                cv::Scalar(0, 0, 0), thickness + 2, cv::LINE_AA);
    cv::putText(image, text, origin, cv::FONT_HERSHEY_SIMPLEX, scale,
                color, thickness, cv::LINE_AA);
}

std::string qualitySuggestion(const std::string& reason) {
    if (reason.rfind("LOW_SCANS", 0) == 0) {
        return "Check light and keep full edges inside the highlighted cell";
    }
    if (reason.rfind("HIGH_SIGMA", 0) == 0) {
        return "Reduce glare/shadow and avoid thread or highlight edges";
    }
    if (reason.rfind("FRAME_JUMP", 0) == 0) {
        return "Ignore while placing; keep the part still during capture";
    }
    return "Check placement, light and edge visibility";
}

} // namespace

void FrameVisualizer::drawOverlay(const AppConfig& cfg,
                                  const CalibrationMapper& calibration,
                                  cv::Mat& image,
                                  const OBBResult& tracked,
                                  const MeasurementResult& last_measurement) const {
    if (tracked.class_id >= 0 && tracked.confidence > 0.0f &&
        tracked.rrect.size.width > 1.0f && tracked.rrect.size.height > 1.0f) {
        cv::Point2f pts[4];
        tracked.rrect.points(pts);
        for (int i = 0; i < 4; ++i) {
            cv::line(image, pts[i], pts[(i + 1) % 4], cv::Scalar(0, 255, 0), 2);
        }
    }

    if (last_measurement.valid) {
        for (size_t i = 0; i < last_measurement.left_edge_samples_px.size(); ++i) {
            cv::circle(image, last_measurement.left_edge_samples_px[i], 2, cv::Scalar(180, 80, 0), -1);
            if (i < last_measurement.right_edge_samples_px.size()) {
                cv::circle(image, last_measurement.right_edge_samples_px[i], 2, cv::Scalar(0, 80, 180), -1);
            }
        }
        cv::circle(image, last_measurement.left_edge_px, 3, cv::Scalar(255, 0, 0), -1);
        cv::circle(image, last_measurement.right_edge_px, 3, cv::Scalar(0, 0, 255), -1);
    }

    std::string px_text = last_measurement.valid ? ("px=" + std::to_string(last_measurement.pixel_distance)) : "px=N/A";
    cv::putText(image, px_text, cv::Point(20, 40), cv::FONT_HERSHEY_SIMPLEX, 0.8,
                cv::Scalar(0, 255, 255), 2);

    std::string mm_text = calibration.canMeasureInMm() ? ("mm=" + std::to_string(last_measurement.world_distance_mm))
                                                        : "mm=N/A (need calibration)";
    cv::putText(image, mm_text, cv::Point(20, 75), cv::FONT_HERSHEY_SIMPLEX, 0.8,
                cv::Scalar(255, 255, 0), 2);

    if (last_measurement.valid && last_measurement.world_sigma_mm >= 0.0f) {
        const std::string sigma_text = "sigma=" + std::to_string(last_measurement.world_sigma_mm) +
                                       " scans=" + std::to_string(last_measurement.valid_scan_count);
        cv::putText(image, sigma_text, cv::Point(20, 110), cv::FONT_HERSHEY_SIMPLEX, 0.8,
                    cv::Scalar(255, 200, 0), 2);
    }

    if (last_measurement.valid && !last_measurement.quality_ok) {
        const std::string quality_text = "LOW QUALITY: " + last_measurement.quality_reason;
        putOutlinedText(image, quality_text, cv::Point(20, 145), 0.8,
                        cv::Scalar(0, 128, 255), 2);
        putOutlinedText(image, qualitySuggestion(last_measurement.quality_reason),
                        cv::Point(20, 180), 0.65, cv::Scalar(0, 200, 255), 2);
    }
}

bool FrameVisualizer::render(const AppConfig& cfg,
                             const CalibrationMapper& calibration,
                             cv::Mat& image,
                             const OBBResult& tracked,
                             const MeasurementResult& last_measurement) const {
    drawOverlay(cfg, calibration, image, tracked, last_measurement);

    prepareDisplayWindow(cfg, image);
    cv::imshow(kWindowName, image);
    int key = cv::waitKey(1);
    if (key == 27 || key == 'q') {
        return true;
    }

    return false;
}
