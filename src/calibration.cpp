#include "calibration.hpp"

#include "logger.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <string>
#include <vector>

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

void hashFile(const fs::path& file_path, uint64_t& hash) {
    std::ifstream ifs(file_path, std::ios::binary);
    char c = 0;
    while (ifs.get(c)) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ull;
    }
}

std::string makeCalibrationFingerprint(const fs::path& campar_path, const fs::path& pose_path) {
    uint64_t hash = 1469598103934665603ull;
    hashFile(campar_path, hash);
    hash ^= 0xffu;
    hash *= 1099511628211ull;
    hashFile(pose_path, hash);

    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return oss.str();
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
    fingerprint_.clear();

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
        logger::Error(std::string("[HALCON] 未找到成对标定文件: dir=") + dir_path);
        return false;
    }
    logger::Info(std::string("[HALCON] 使用标定文件: campar=") + campar.string() + ", pose=" + pose.string());
    return loadFromFiles(campar.string(), pose.string());
}

bool CalibrationMapper::loadFromFiles(const std::string& campar_path, const std::string& pose_path) {
#ifdef USE_HALCON
    const fs::path temp_dir = fs::temp_directory_path() / "hik_yoloobb_halcon_calib";
    const fs::path temp_campar = temp_dir / "camera_parameters.cal";
    const fs::path temp_pose = temp_dir / "camera_pose.dat";
    std::string halcon_campar_path = campar_path;
    std::string halcon_pose_path = pose_path;

    try {
        std::error_code ec;
        fs::create_directories(temp_dir, ec);
        if (ec) {
            logger::Warn(std::string("[HALCON] 创建临时标定目录失败: ") + temp_dir.string() +
                         ", error=" + ec.message());
        } else {
            fs::copy_file(campar_path, temp_campar, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                logger::Warn(std::string("[HALCON] 复制相机参数到临时路径失败: src=") + campar_path +
                             ", dst=" + temp_campar.string() + ", error=" + ec.message());
            } else {
                halcon_campar_path = temp_campar.string();
            }

            ec.clear();
            fs::copy_file(pose_path, temp_pose, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                logger::Warn(std::string("[HALCON] 复制位姿到临时路径失败: src=") + pose_path +
                             ", dst=" + temp_pose.string() + ", error=" + ec.message());
            } else {
                halcon_pose_path = temp_pose.string();
            }
        }
    } catch (const std::exception& e) {
        logger::Warn(std::string("[HALCON] 准备 ASCII 临时标定路径异常: ") + e.what());
    }

    try {
        logger::Info(std::string("[HALCON] 读取标定临时路径: campar=") + halcon_campar_path +
                     ", pose=" + halcon_pose_path);
        HalconCpp::ReadCamPar(halcon_campar_path.c_str(), &cam_param_);
        HalconCpp::ReadPose(halcon_pose_path.c_str(), &world_pose_);
        fingerprint_ = makeCalibrationFingerprint(campar_path, pose_path);
        ready_ = true;
        return true;
    } catch (const HalconCpp::HException& e) {
        logger::Error(std::string("[HALCON] 读取标定失败: campar=") + campar_path + ", pose=" + pose_path);
        logger::Error(std::string("[HALCON] 临时路径: campar=") + halcon_campar_path +
                      ", pose=" + halcon_pose_path);
        logger::Error(std::string("[HALCON] 异常: ") + e.ErrorMessage().Text());
        ready_ = false;
        projection_valid_ = false;
        fingerprint_.clear();
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
            logger::Error("[HALCON] 投影校验失败: 返回长度为空");
            return false;
        }

        const double dx = x1[0].D() - x0[0].D();
        const double dy = y1[0].D() - y0[0].D();
        const double dist = std::sqrt(dx * dx + dy * dy);
        if (!std::isfinite(dist) || dist <= 1e-6) {
            logger::Error(std::string("[HALCON] 投影校验失败: dist=") + std::to_string(dist));
            return false;
        }
        logger::Info(std::string("[HALCON] 投影校验通过: 100px -> ") + std::to_string(dist) + " mm");
        return true;
    } catch (const HalconCpp::HException& e) {
        logger::Error(std::string("[HALCON] 投影校验异常: ") + e.ErrorMessage().Text());
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

const std::string& CalibrationMapper::fingerprint() const {
    return fingerprint_;
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

void CalibrationMapper::logScaleDiagnostics(int image_width, int image_height) const {
    if (!canMeasureInMm() || image_width <= 0 || image_height <= 0) {
        return;
    }

    struct Probe {
        const char* name = "";
        cv::Point2f p;
    };

    const float margin_x = std::max(50.0f, static_cast<float>(image_width) * 0.12f);
    const float margin_y = std::max(50.0f, static_cast<float>(image_height) * 0.12f);
    const std::vector<Probe> probes = {
        {"center", cv::Point2f(image_width * 0.5f, image_height * 0.5f)},
        {"left_top", cv::Point2f(margin_x, margin_y)},
        {"right_top", cv::Point2f(image_width - margin_x, margin_y)},
        {"left_bottom", cv::Point2f(margin_x, image_height - margin_y)},
        {"right_bottom", cv::Point2f(image_width - margin_x, image_height - margin_y)},
    };

    const float step = std::min(100.0f, std::max(20.0f, std::min(image_width, image_height) * 0.08f));
    for (const Probe& probe : probes) {
        const cv::Point2f p0(std::clamp(probe.p.x, 0.0f, static_cast<float>(image_width - 1)),
                             std::clamp(probe.p.y, 0.0f, static_cast<float>(image_height - 1)));
        const cv::Point2f px(std::clamp(p0.x + step, 0.0f, static_cast<float>(image_width - 1)), p0.y);
        const cv::Point2f py(p0.x, std::clamp(p0.y + step, 0.0f, static_cast<float>(image_height - 1)));

        const cv::Point2f w0 = pixelToWorld(p0);
        const cv::Point2f wx = pixelToWorld(px);
        const cv::Point2f wy = pixelToWorld(py);
        if (!std::isfinite(w0.x) || !std::isfinite(w0.y) ||
            !std::isfinite(wx.x) || !std::isfinite(wx.y) ||
            !std::isfinite(wy.x) || !std::isfinite(wy.y)) {
            continue;
        }

        const double sx = cv::norm(wx - w0) / std::max(1.0f, px.x - p0.x);
        const double sy = cv::norm(wy - w0) / std::max(1.0f, py.y - p0.y);
        logger::Info(std::string("[CALIB] local scale ") + probe.name +
                     ": sx=" + std::to_string(sx) +
                     " mm/px, sy=" + std::to_string(sy) + " mm/px");
    }
}
