#pragma once

#include "calibration.hpp"
#include "config.hpp"
#include "types.hpp"

class FrameVisualizer {
public:
    void drawOverlay(const AppConfig& cfg,
                     const CalibrationMapper& calibration,
                     cv::Mat& image,
                     const OBBResult& tracked,
                     const MeasurementResult& last_measurement) const;

    bool render(const AppConfig& cfg,
                const CalibrationMapper& calibration,
                cv::Mat& image,
                const OBBResult& tracked,
                const MeasurementResult& last_measurement) const;
};
