#include "app/frame_processor.hpp"

#include "app/frame_visualizer.hpp"
#include "calibration.hpp"
#include "onnx_inferencer.hpp"
#include "subpixel_caliper.hpp"
#include "tracker_ekf.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr size_t kSmoothWindow = 5;

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

void updateWorldDistance(const CalibrationMapper& calibration, MeasurementResult& mr) {
    if (!calibration.canMeasureInMm()) {
        mr.world_distance_mm = -1.0f;
        return;
    }

    const cv::Point2f w0 = calibration.pixelToWorld(mr.left_edge_px);
    const cv::Point2f w1 = calibration.pixelToWorld(mr.right_edge_px);
    const float mm = cv::norm(w1 - w0);
    if (std::isfinite(mm) && mm > 0.0f) {
        mr.world_distance_mm = mm;
    } else {
        mr.world_distance_mm = -1.0f;
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
        mr = context.caliper->measure(frame, tracked);
    }

    if (mr.valid) {
        updateWorldDistance(*context.calibration, mr);
        smoothMeasurement(mr, state);
        state.last_measurement = mr;
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
