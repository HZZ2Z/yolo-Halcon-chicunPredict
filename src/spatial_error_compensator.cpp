#include "spatial_error_compensator.hpp"

#include "logger.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

namespace {

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::vector<std::string> splitCsvLine(std::string line) {
    for (char& c : line) {
        if (c == ',' || c == ';' || c == '\t') {
            c = ' ';
        }
    }
    std::istringstream iss(line);
    std::vector<std::string> out;
    std::string token;
    while (iss >> token) {
        out.push_back(token);
    }
    return out;
}

bool parseFloat(const std::string& text, float& value) {
    try {
        size_t idx = 0;
        value = std::stof(text, &idx);
        return idx == text.size() && std::isfinite(value);
    } catch (...) {
        return false;
    }
}

int findColumn(const std::vector<std::string>& header, const std::vector<std::string>& names) {
    for (size_t i = 0; i < header.size(); ++i) {
        const std::string h = lowerCopy(header[i]);
        for (const std::string& name : names) {
            if (h == name) {
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

}  // namespace

bool SpatialErrorCompensator::load(const std::string& path) {
    samples_.clear();
    if (path.empty()) {
        return false;
    }

    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        logger::Warn(std::string("[COMP] 无法打开残差补偿文件: ") + path);
        return false;
    }

    int x_col = 0;
    int y_col = 1;
    int corr_col = 2;
    int error_col = -1;
    bool header_checked = false;

    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const std::vector<std::string> cols = splitCsvLine(line);
        if (cols.size() < 3) {
            continue;
        }

        if (!header_checked) {
            header_checked = true;
            float first_value = 0.0f;
            if (!parseFloat(cols[0], first_value)) {
                x_col = findColumn(cols, {"x", "cx", "center_x", "target_center_x"});
                y_col = findColumn(cols, {"y", "cy", "center_y", "target_center_y"});
                corr_col = findColumn(cols, {"correction_mm", "correction", "corr_mm"});
                error_col = findColumn(cols, {"error_mm", "error", "residual_mm", "residual"});
                if (x_col < 0 || y_col < 0 || (corr_col < 0 && error_col < 0)) {
                    logger::Warn("[COMP] 残差补偿表头缺少 x/y/correction_mm 或 error_mm");
                    return false;
                }
                continue;
            }
        }

        const int value_col = corr_col >= 0 ? corr_col : error_col;
        const int max_col = std::max({x_col, y_col, value_col});
        if (max_col < 0 || max_col >= static_cast<int>(cols.size())) {
            continue;
        }

        float x = 0.0f;
        float y = 0.0f;
        float value = 0.0f;
        if (!parseFloat(cols[x_col], x) || !parseFloat(cols[y_col], y) || !parseFloat(cols[value_col], value)) {
            continue;
        }

        Sample sample;
        sample.position = cv::Point2f(x, y);
        sample.correction_mm = corr_col >= 0 ? value : -value;
        samples_.push_back(sample);
    }

    if (samples_.empty()) {
        logger::Warn(std::string("[COMP] 残差补偿文件没有有效样本: ") + path);
        return false;
    }

    logger::Info(std::string("[COMP] 已加载空间残差补偿样本: ") + std::to_string(samples_.size()));
    return true;
}

bool SpatialErrorCompensator::ready() const {
    return !samples_.empty();
}

float SpatialErrorCompensator::correction(float x, float y) const {
    if (samples_.empty()) {
        return 0.0f;
    }

    struct Neighbor {
        float dist2 = 0.0f;
        float correction = 0.0f;
    };

    std::vector<Neighbor> neighbors;
    neighbors.reserve(samples_.size());
    const cv::Point2f p(x, y);
    for (const Sample& sample : samples_) {
        const cv::Point2f d = sample.position - p;
        const float dist2 = d.x * d.x + d.y * d.y;
        if (dist2 < 1e-6f) {
            return sample.correction_mm;
        }
        neighbors.push_back({dist2, sample.correction_mm});
    }

    std::partial_sort(neighbors.begin(),
                      neighbors.begin() + std::min<size_t>(4, neighbors.size()),
                      neighbors.end(),
                      [](const Neighbor& a, const Neighbor& b) {
                          return a.dist2 < b.dist2;
                      });

    const size_t count = std::min<size_t>(4, neighbors.size());
    double wsum = 0.0;
    double csum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const double w = 1.0 / std::max(1e-6f, neighbors[i].dist2);
        wsum += w;
        csum += w * neighbors[i].correction;
    }

    return wsum > 0.0 ? static_cast<float>(csum / wsum) : 0.0f;
}

float SpatialErrorCompensator::apply(float measured_mm, float x, float y) const {
    return measured_mm + correction(x, y);
}
