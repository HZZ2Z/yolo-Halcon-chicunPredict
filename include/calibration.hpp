#pragma once

#ifdef USE_HALCON
#include <halconcpp/HalconCpp.h>
#endif
#include <opencv2/opencv.hpp>
#include <string>

class CalibrationMapper {
public:
    bool load(const std::string& calibration_path,
              int image_width = 1280,
              int image_height = 1024,
              double focal_length_mm = 8.0,
              double pixel_size_um = 4.0);
    cv::Mat undistort(const cv::Mat& src) const;
    cv::Point2f pixelToWorld(const cv::Point2f& px) const;
    bool hasHomography() const;
    bool canMeasureInMm() const;
    void logScaleDiagnostics(int image_width, int image_height) const;

private:
    bool loadFromDirectory(const std::string& dir_path);
    bool loadFromFiles(const std::string& campar_path, const std::string& pose_path);
    bool validateProjection(int image_width, int image_height);

#ifdef USE_HALCON
    HalconCpp::HTuple cam_param_;
    HalconCpp::HTuple world_pose_;
#endif
    bool ready_ = false;
    bool projection_valid_ = false;
};
