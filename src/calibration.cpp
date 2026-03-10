#include "calibration.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

namespace fs = std::filesystem;

namespace {

bool isLikelyPoseDat(const fs::path& file_path) {
    const std::string name = file_path.filename().string();
    if (name.find("位姿") != std::string::npos || name.find("pose") != std::string::npos ||
        name.find("Pose") != std::string::npos) {
        return true;
    }

    std::ifstream ifs(file_path, std::ios::binary);
    if (!ifs.is_open()) {
        return false;
    }
    std::string buf(1024, '\0');
    ifs.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    buf.resize(static_cast<size_t>(ifs.gcount()));
    if (buf.empty()) {
        return false;
    }
    return (buf.find("\nr ") != std::string::npos || buf.find("\nr\t") != std::string::npos) &&
           (buf.find("\nt ") != std::string::npos || buf.find("\nt\t") != std::string::npos ||
            buf.find("Translation vector") != std::string::npos);
}

} // namespace

bool CalibrationMapper::load(const std::string& calibration_path,
                             int image_width,
                             int image_height,
                             double focal_length_mm,
                             double pixel_size_um) {
    (void)focal_length_mm;
    (void)pixel_size_um;

    ready_ = false;
    projection_valid_ = false;

    if (calibration_path.empty()) {
        return false;
    }

    fs::path p(calibration_path);
    if (fs::is_directory(p)) {
        const bool loaded = loadFromDirectory(calibration_path);
        projection_valid_ = loaded && validateProjection(image_width, image_height);
        return loaded && projection_valid_;
    }

    if (p.extension() == ".cal") {
        fs::path pose = p.parent_path() / "摄像机位姿.dat";
        if (!fs::exists(pose)) {
            for (const auto& entry : fs::directory_iterator(p.parent_path())) {
                if (entry.is_regular_file() && entry.path().extension() == ".dat") {
                    pose = entry.path();
                    break;
                }
            }
        }
        const bool loaded = loadFromFiles(p.string(), pose.string());
        projection_valid_ = loaded && validateProjection(image_width, image_height);
        return loaded && projection_valid_;
    }

    return false;
}

bool CalibrationMapper::loadFromDirectory(const std::string& dir_path) {
    fs::path dir(dir_path);
    fs::path campar;
    fs::path pose;
    fs::path fallback_pose;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto ext = entry.path().extension().string();
        if (campar.empty() && ext == ".cal") {
            campar = entry.path();
            continue;
        }
        if (ext == ".dat") {
            if (isLikelyPoseDat(entry.path())) {
                pose = entry.path();
                continue;
            }
            if (fallback_pose.empty()) {
                fallback_pose = entry.path();
            }
            continue;
        }
    }

    if (pose.empty()) {
        pose = fallback_pose;
    }

    if (campar.empty() || pose.empty()) {
        std::cerr << "[HALCON] 未找到成对标定文件: dir=" << dir_path << std::endl;
        return false;
    }
    std::cerr << "[HALCON] 使用标定文件: campar=" << campar.string() << ", pose=" << pose.string() << std::endl;
    return loadFromFiles(campar.string(), pose.string());
}

bool CalibrationMapper::loadFromFiles(const std::string& campar_path, const std::string& pose_path) {
#ifdef USE_HALCON
    try {
        HalconCpp::ReadCamPar(campar_path.c_str(), &cam_param_);
        HalconCpp::ReadPose(pose_path.c_str(), &world_pose_);
        ready_ = true;
        return true;
    } catch (const HalconCpp::HException& e) {
        std::cerr << "[HALCON] 读取标定失败: campar=" << campar_path << ", pose=" << pose_path << std::endl;
        std::cerr << "[HALCON] 异常: " << e.ErrorMessage() << std::endl;
        ready_ = false;
        projection_valid_ = false;
        return false;
    }
#else
    (void)campar_path;
    (void)pose_path;
    return false;
#endif
}

bool CalibrationMapper::validateProjection(int image_width, int image_height) {
#ifdef USE_HALCON
    if (!ready_) {
        return false;
    }
    try {
        const double cx = static_cast<double>(image_width) * 0.5;
        const double cy = static_cast<double>(image_height) * 0.5;

        HalconCpp::HTuple x0;
        HalconCpp::HTuple y0;
        HalconCpp::HTuple x1;
        HalconCpp::HTuple y1;

        HalconCpp::ImagePointsToWorldPlane(cam_param_, world_pose_, cy, cx, "mm", &x0, &y0);
        HalconCpp::ImagePointsToWorldPlane(cam_param_, world_pose_, cy, cx + 100.0, "mm", &x1, &y1);

        if (x0.Length() <= 0 || y0.Length() <= 0 || x1.Length() <= 0 || y1.Length() <= 0) {
            std::cerr << "[HALCON] 投影校验失败: 返回长度为空" << std::endl;
            return false;
        }

        const double dx = x1[0].D() - x0[0].D();
        const double dy = y1[0].D() - y0[0].D();
        const double dist = std::sqrt(dx * dx + dy * dy);
        if (!std::isfinite(dist) || dist <= 1e-6) {
            std::cerr << "[HALCON] 投影校验失败: dist=" << dist << std::endl;
            return false;
        }
        std::cerr << "[HALCON] 投影校验通过: 100px -> " << dist << " mm" << std::endl;
        return true;
    } catch (const HalconCpp::HException& e) {
        std::cerr << "[HALCON] 投影校验异常: " << e.ErrorMessage() << std::endl;
        return false;
    }
#else
    (void)image_width;
    (void)image_height;
    return false;
#endif
}

cv::Mat CalibrationMapper::undistort(const cv::Mat& src) const {
    return src;
}

bool CalibrationMapper::hasHomography() const {
    return projection_valid_;
}

bool CalibrationMapper::canMeasureInMm() const {
    return projection_valid_;
}

cv::Point2f CalibrationMapper::pixelToWorld(const cv::Point2f& px) const {
#ifdef USE_HALCON
    if (!projection_valid_) {
        return cv::Point2f(std::numeric_limits<float>::quiet_NaN(),
                           std::numeric_limits<float>::quiet_NaN());
    }

    try {
        HalconCpp::HTuple row = static_cast<double>(px.y);
        HalconCpp::HTuple col = static_cast<double>(px.x);
        HalconCpp::HTuple x;
        HalconCpp::HTuple y;
        HalconCpp::ImagePointsToWorldPlane(cam_param_, world_pose_, row, col, "mm", &x, &y);
        if (x.Length() > 0 && y.Length() > 0) {
            return cv::Point2f(static_cast<float>(x[0].D()), static_cast<float>(y[0].D()));
        }
    } catch (const HalconCpp::HException&) {
        return cv::Point2f(std::numeric_limits<float>::quiet_NaN(),
                           std::numeric_limits<float>::quiet_NaN());
    }
#endif
    (void)px;
    return cv::Point2f(std::numeric_limits<float>::quiet_NaN(),
                       std::numeric_limits<float>::quiet_NaN());
}
