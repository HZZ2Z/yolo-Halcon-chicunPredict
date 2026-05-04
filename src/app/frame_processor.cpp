#include "app/frame_processor.hpp"

#include "app/frame_visualizer.hpp"
#include "calibration.hpp"
#include "logger.hpp"
#include "measurement_uncertainty.hpp"
#include "onnx_inferencer.hpp"
#include "spatial_error_compensator.hpp"
#include "subpixel_caliper.hpp"
#include "tracker_ekf.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace {

constexpr size_t kSmoothWindow = 5;

std::string formatFloat(float value, int precision = 3) {
    if (!std::isfinite(value)) {
        return "N/A";
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

float medianValue(const std::deque<float>& values) {
    if (values.empty()) {
        return 0.0f;
    }
    std::vector<float> sorted(values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());
    const size_t n = sorted.size();
    if (n % 2 == 1) {
        return sorted[n / 2];
    }
    return 0.5f * (sorted[n / 2 - 1] + sorted[n / 2]);
}

void smoothMeasurement(MeasurementResult& mr, FrameProcessorState& state) {
    state.px_history.push_back(mr.pixel_distance);
    if (state.px_history.size() > kSmoothWindow) {
        state.px_history.pop_front();
    }
    mr.pixel_distance = medianValue(state.px_history);

    if (mr.world_distance_mm > 0.0f && std::isfinite(mr.world_distance_mm)) {
        state.mm_history.push_back(mr.world_distance_mm);
        if (state.mm_history.size() > kSmoothWindow) {
            state.mm_history.pop_front();
        }
        mr.world_distance_mm = medianValue(state.mm_history);
    }
}

std::string evaluateMeasurementQuality(const AppConfig& cfg,
                                       const MeasurementResult& mr,
                                       const FrameProcessorState& state) {
    if (cfg.min_valid_scan_count > 0 && mr.valid_scan_count < cfg.min_valid_scan_count) {
        return "LOW_SCANS scans=" + std::to_string(mr.valid_scan_count) +
               "/" + std::to_string(cfg.min_valid_scan_count);
    }
    if (mr.world_sigma_mm >= 0.0f && std::isfinite(mr.world_sigma_mm) &&
        mr.world_sigma_mm > cfg.max_sigma_mm) {
        return "HIGH_SIGMA sigma=" + formatFloat(mr.world_sigma_mm) +
               "mm > " + formatFloat(cfg.max_sigma_mm) + "mm";
    }
    if (!state.mm_history.empty() && mr.world_distance_mm > 0.0f &&
        std::isfinite(mr.world_distance_mm)) {
        const float stable_mm = medianValue(state.mm_history);
        const float jump_mm = std::abs(mr.world_distance_mm - stable_mm);
        if (jump_mm > cfg.max_frame_jump_mm) {
            return "FRAME_JUMP jump=" + formatFloat(jump_mm) +
                   "mm > " + formatFloat(cfg.max_frame_jump_mm) + "mm";
        }
    }
    return "OK";
}

void ensureCsvOpen(const AppConfig& cfg, FrameProcessorState& state) {
    if (!cfg.enable_measurement_csv || state.csv.is_open()) {
        return;
    }

    const std::filesystem::path path(cfg.measurement_csv_path);
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    const bool needs_header =
        !std::filesystem::exists(path) || std::filesystem::file_size(path) == 0;
    state.csv.open(path, std::ios::app);
    if (!state.csv.is_open()) {
        logger::Warn(std::string("[CSV] 无法打开测量记录文件: ") + cfg.measurement_csv_path);
        return;
    }
    state.csv_header_written = !needs_header;
    if (!state.csv_header_written) {
        state.csv << "frame_id,cx,cy,angle,px,raw_mm,mm,sigma,scans,quality,"
                     "correction_mm,true_mm,error_mm\n";
        state.csv_header_written = true;
    }
}

void writeMeasurementCsv(const AppConfig& cfg,
                         FrameProcessorState& state,
                         const MeasurementResult& mr,
                         const OBBResult& tracked) {
    if (!cfg.enable_measurement_csv) {
        return;
    }
    ensureCsvOpen(cfg, state);
    if (!state.csv.is_open()) {
        return;
    }

    const float true_mm = cfg.standard_true_mm > 0.0f ? cfg.standard_true_mm : -1.0f;
    const float error_mm =
        (true_mm > 0.0f && mr.raw_world_distance_mm > 0.0f)
            ? (mr.raw_world_distance_mm - true_mm)
            : -1.0f;

    state.csv << std::fixed << std::setprecision(6)
              << mr.frame_id << ','
              << tracked.rrect.center.x << ','
              << tracked.rrect.center.y << ','
              << tracked.rrect.angle << ','
              << mr.pixel_distance << ','
              << mr.raw_world_distance_mm << ','
              << mr.world_distance_mm << ','
              << mr.world_sigma_mm << ','
              << mr.valid_scan_count << ','
              << mr.quality_reason << ','
              << mr.correction_mm << ','
              << true_mm << ','
              << error_mm << '\n';
    state.csv.flush();
}

bool isFinitePoint(const cv::Point2f& p) {
    return std::isfinite(p.x) && std::isfinite(p.y);
}

cv::Matx22d toDoubleMat(const cv::Matx22f& m) {
    return cv::Matx22d(m(0, 0), m(0, 1), m(1, 0), m(1, 1));
}

cv::Matx22d propagatePixelCovariance(const CalibrationMapper& calibration,
                                      const cv::Point2f& px,
                                      const cv::Matx22f& cov_px) {
    const cv::Matx22d jac = NumericalPixelJacobianWorld(
        calibration, cv::Point2d(static_cast<double>(px.x), static_cast<double>(px.y)));
    return jac * toDoubleMat(cov_px) * jac.t();
}

void updateWorldDistance(const CalibrationMapper& calibration,
                         MeasurementResult& mr,
                         float huber_delta_mm,
                         bool estimate_uncertainty) {
    if (!calibration.canMeasureInMm()) {
        mr.world_distance_mm = -1.0f;
        mr.raw_world_distance_mm = -1.0f;
        mr.world_sigma_mm = -1.0f;
        return;
    }

    std::vector<cv::Point2f> left_px = mr.left_edge_samples_px;
    std::vector<cv::Point2f> right_px = mr.right_edge_samples_px;
    std::vector<cv::Matx22f> left_cov = mr.left_edge_sample_covs_px;
    std::vector<cv::Matx22f> right_cov = mr.right_edge_sample_covs_px;

    if (left_px.empty() || right_px.empty() || left_px.size() != right_px.size()) {
        left_px = {mr.left_edge_px};
        right_px = {mr.right_edge_px};
        left_cov = {mr.left_edge_cov_px};
        right_cov = {mr.right_edge_cov_px};
    }

    std::vector<cv::Point2d> left_world;
    std::vector<cv::Point2d> right_world;
    std::vector<cv::Matx22d> left_world_cov;
    std::vector<cv::Matx22d> right_world_cov;
    left_world.reserve(left_px.size());
    right_world.reserve(right_px.size());

    cv::Point2d delta_sum(0.0, 0.0);
    for (size_t i = 0; i < left_px.size(); ++i) {
        const cv::Point2f wl = calibration.pixelToWorld(left_px[i]);
        const cv::Point2f wr = calibration.pixelToWorld(right_px[i]);
        if (!isFinitePoint(wl) || !isFinitePoint(wr)) {
            continue;
        }
        left_world.emplace_back(wl.x, wl.y);
        right_world.emplace_back(wr.x, wr.y);
        delta_sum += cv::Point2d(wr.x - wl.x, wr.y - wl.y);
        if (estimate_uncertainty && i < left_cov.size() && i < right_cov.size()) {
            left_world_cov.push_back(propagatePixelCovariance(calibration, left_px[i], left_cov[i]));
            right_world_cov.push_back(propagatePixelCovariance(calibration, right_px[i], right_cov[i]));
        } else {
            left_world_cov.push_back(cv::Matx22d::eye());
            right_world_cov.push_back(cv::Matx22d::eye());
        }
    }

    const double delta_norm = std::sqrt(delta_sum.x * delta_sum.x + delta_sum.y * delta_sum.y);
    if (left_world.empty() || delta_norm <= 1e-12 || !std::isfinite(delta_norm)) {
        mr.world_distance_mm = -1.0f;
        mr.raw_world_distance_mm = -1.0f;
        mr.world_sigma_mm = -1.0f;
        return;
    }

    const cv::Vec2d axis(delta_sum.x / delta_norm, delta_sum.y / delta_norm);
    std::vector<double> left_values;
    std::vector<double> right_values;
    std::vector<double> left_vars;
    std::vector<double> right_vars;
    for (size_t i = 0; i < left_world.size(); ++i) {
        left_values.push_back(axis[0] * left_world[i].x + axis[1] * left_world[i].y);
        right_values.push_back(axis[0] * right_world[i].x + axis[1] * right_world[i].y);
        left_vars.push_back(estimate_uncertainty ? ProjectVarianceAlongNormal(left_world_cov[i], axis) : 1.0);
        right_vars.push_back(estimate_uncertainty ? ProjectVarianceAlongNormal(right_world_cov[i], axis) : 1.0);
    }

    const RobustMeanResult left_mean =
        HuberWeightedMean(left_values, left_vars, static_cast<double>(huber_delta_mm), 5);
    const RobustMeanResult right_mean =
        HuberWeightedMean(right_values, right_vars, static_cast<double>(huber_delta_mm), 5);

    const double mm = std::abs(right_mean.mean - left_mean.mean);
    if (std::isfinite(mm) && mm > 0.0) {
        mr.raw_world_distance_mm = static_cast<float>(mm);
        mr.world_distance_mm = static_cast<float>(mm);
        if (estimate_uncertainty && left_mean.variance >= 0.0 && right_mean.variance >= 0.0) {
            mr.world_sigma_mm = static_cast<float>(std::sqrt(left_mean.variance + right_mean.variance));
        } else {
            mr.world_sigma_mm = -1.0f;
        }
    } else {
        mr.world_distance_mm = -1.0f;
        mr.raw_world_distance_mm = -1.0f;
        mr.world_sigma_mm = -1.0f;
    }
}

bool isValidTrackedObb(const OBBResult& tracked) {
    return tracked.class_id >= 0 && tracked.confidence > 0.0f &&
           tracked.rrect.size.width > 1.0f && tracked.rrect.size.height > 1.0f;
}

}  // namespace

bool ProcessFrame(FrameData frame,
                  const AppConfig& cfg,
                  PipelineContext& context,
                  FrameProcessorState& state,
                  std::atomic<bool>& stop_requested,
                  ThreadSafeQueue<FrameData>& queue) {
    ++state.processed_frames;
    frame.image = context.calibration->undistort(frame.image);

    std::optional<OBBResult> best;
    const bool do_infer =
        ((state.processed_frames - 1) % static_cast<uint64_t>(cfg.infer_interval) == 0);
    if (do_infer) {
        auto detections = context.inferencer->infer(frame.image);
        if (!detections.empty()) {
            best = *std::max_element(detections.begin(), detections.end(),
                                     [](const OBBResult& a, const OBBResult& b) {
                                         return a.confidence < b.confidence;
                                     });
        }
    }

    OBBResult tracked = context.tracker->update(best);
    MeasurementResult mr;

    const bool do_measure =
        ((state.processed_frames - 1) % static_cast<uint64_t>(cfg.measure_interval) == 0);
    if (do_measure && isValidTrackedObb(tracked)) {
        const cv::Matx33f pose_cov = context.tracker->poseCovariance();
        mr = context.caliper->measure(frame, tracked, &pose_cov);
    }

    if (mr.valid) {
        updateWorldDistance(*context.calibration,
                            mr,
                            cfg.huber_delta_mm,
                            cfg.estimate_measurement_uncertainty);
        if (context.compensator && context.compensator->ready() &&
            mr.raw_world_distance_mm > 0.0f && std::isfinite(mr.raw_world_distance_mm)) {
            const float corr = context.compensator->correction(tracked.rrect.center.x,
                                                               tracked.rrect.center.y);
            mr.correction_mm = corr;
            mr.raw_world_distance_mm += corr;
            mr.world_distance_mm += corr;
        }

        const std::string quality = evaluateMeasurementQuality(cfg, mr, state);
        mr.quality_ok = quality == "OK";
        mr.quality_reason = quality;
        if (mr.quality_ok) {
            state.rejected_measurement_count = 0;
            smoothMeasurement(mr, state);
            state.last_measurement = mr;
        } else {
            ++state.rejected_measurement_count;
            logger::Warn(std::string("[MEASURE] 低质量帧已跳过: reason=") + quality +
                         ", sigma=" + std::to_string(mr.world_sigma_mm) +
                         ", scans=" + std::to_string(mr.valid_scan_count));
            state.last_measurement = mr;
        }
        writeMeasurementCsv(cfg, state, mr, tracked);
    }

    if (cfg.show_window) {
        if ((state.processed_frames - 1) % static_cast<uint64_t>(cfg.render_interval) != 0) {
            return false;
        }
        if (context.visualizer->render(cfg,
                                       *context.calibration,
                                       frame.image,
                                       tracked,
                                       state.last_measurement)) {
            stop_requested.store(true);
            queue.close();
            return true;
        }
    }

    return false;
}
