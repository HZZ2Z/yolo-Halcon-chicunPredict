#include "app/frame_visualizer.hpp"

#include <opencv2/opencv.hpp>

#include <string>

bool FrameVisualizer::render(const AppConfig& cfg,
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

    cv::imshow("metal_metrology", image);
    int key = cv::waitKey(1);
    if (key == 27 || key == 'q') {
        return true;
    }

    return false;
}
