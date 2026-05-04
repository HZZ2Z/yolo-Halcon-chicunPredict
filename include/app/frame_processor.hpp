#pragma once

#include "app/pipeline_context.hpp"
#include "config.hpp"
#include "thread_safe_queue.hpp"
#include "types.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <fstream>

struct FrameProcessorState {
    uint64_t processed_frames = 0;
    MeasurementResult last_measurement;
    std::deque<float> px_history;
    std::deque<float> mm_history;
    std::ofstream csv;
    bool csv_header_written = false;
    int rejected_measurement_count = 0;
};

bool ProcessFrame(FrameData frame,
                  const AppConfig& cfg,
                  PipelineContext& context,
                  FrameProcessorState& state,
                  std::atomic<bool>& stop_requested,
                  ThreadSafeQueue<FrameData>& queue);
