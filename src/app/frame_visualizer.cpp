#include "app/frame_visualizer.hpp"

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

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

void drawGridGuide(cv::Mat& image, const std::string& position) {
    static const std::array<std::array<const char*, 3>, 3> labels{{
        {{"LT", "CT", "RT"}},
        {{"LC", "C", "RC"}},
        {{"LB", "CB", "RB"}},
    }};

    if (image.empty()) {
        return;
    }

    std::string target = position;
    std::transform(target.begin(), target.end(), target.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    if (target.empty()) {
        target = "C";
    }

    const int w = image.cols;
    const int h = image.rows;
    const int left = std::max(8, w / 14);
    const int right = std::min(w - 8, w - w / 14);
    const int top = std::max(8, h / 11);
    const int bottom = std::min(h - 70, h - h / 11);
    const int grid_w = right - left;
    const int grid_h = bottom - top;

    cv::Mat overlay = image.clone();
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const int x0 = left + grid_w * col / 3;
            const int x1 = left + grid_w * (col + 1) / 3;
            const int y0 = top + grid_h * row / 3;
            const int y1 = top + grid_h * (row + 1) / 3;
            const std::string label = labels[row][col];
            const bool active = label == target;
            const cv::Scalar color = active ? cv::Scalar(0, 220, 255) : cv::Scalar(80, 220, 80);
            const int thickness = active ? 4 : 2;

            if (active) {
                cv::rectangle(overlay, cv::Rect(cv::Point(x0, y0), cv::Point(x1, y1)),
                              cv::Scalar(0, 120, 255), cv::FILLED);
            }
            cv::rectangle(image, cv::Point(x0, y0), cv::Point(x1, y1), color, thickness);

            const cv::Point center((x0 + x1) / 2, (y0 + y1) / 2);
            cv::drawMarker(image, center, color, cv::MARKER_CROSS, active ? 28 : 18, thickness);
            putOutlinedText(image, label, cv::Point(center.x - 22, center.y - 12),
                            active ? 0.9 : 0.6, color, thickness);
        }
    }

    cv::addWeighted(overlay, 0.22, image, 0.78, 0.0, image);
    cv::rectangle(image, cv::Point(left, top), cv::Point(right, bottom), cv::Scalar(80, 220, 80), 2);
    putOutlinedText(image, "GRID POSITION: " + target, cv::Point(left, std::max(28, top - 16)),
                    0.9, cv::Scalar(0, 220, 255), 2);
}

void drawGridInstructions(cv::Mat& image, const std::string& position) {
    if (image.empty()) {
        return;
    }

    std::string target = position.empty() ? "C" : position;
    std::transform(target.begin(), target.end(), target.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });

    const std::vector<std::string> lines = {
        "Place the standard part in grid " + target,
        "Adjust until OBB and edge points are stable",
        "Press q or Esc to finish preview"
    };

    const int panel_h = 92;
    const int y0 = std::max(0, image.rows - panel_h - 8);
    cv::Mat overlay = image.clone();
    cv::rectangle(overlay, cv::Point(12, y0), cv::Point(image.cols - 12, image.rows - 12),
                  cv::Scalar(20, 20, 20), cv::FILLED);
    cv::addWeighted(overlay, 0.42, image, 0.58, 0.0, image);

    int y = y0 + 26;
    for (const std::string& line : lines) {
        putOutlinedText(image, line, cv::Point(26, y), 0.65, cv::Scalar(255, 255, 255), 2);
        y += 26;
    }
}

} // namespace

bool FrameVisualizer::render(const AppConfig& cfg,
                             const CalibrationMapper& calibration,
                             cv::Mat& image,
                             const OBBResult& tracked,
                             const MeasurementResult& last_measurement) const {
    if (cfg.show_grid_guide) {
        drawGridGuide(image, cfg.grid_guide_position);
        drawGridInstructions(image, cfg.grid_guide_position);
    }

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

    prepareDisplayWindow(cfg, image);
    cv::imshow(kWindowName, image);
    int key = cv::waitKey(1);
    if (key == 27 || key == 'q') {
        return true;
    }

    return false;
}
