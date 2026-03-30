#include "app/pipeline_runner.hpp"

#include "app/frame_processor.hpp"
#include "app/pipeline_context.hpp"
#include "app/pipeline_init.hpp"
#include "camera_provider.hpp"
#include "logger.hpp"
#include "thread_safe_queue.hpp"
#include "types.hpp"

#include <atomic>
#include <thread>

int RunPipeline(const AppConfig& cfg) {
    PipelineContext context;
    if (!InitializePipeline(cfg, context)) {
        return 1;
    }

    ThreadSafeQueue<FrameData> queue(cfg.queue_size, true);
    std::atomic<bool> stop_requested(false);

    std::thread producer([&]() {
        uint64_t fid = 0;
        while (!stop_requested.load()) {
            if (cfg.max_frames > 0 && fid >= static_cast<uint64_t>(cfg.max_frames)) {
                break;
            }
            FrameData frame;
            if (!context.camera->read(frame, fid)) {
                break;
            }
            queue.push(std::move(frame));
            ++fid;
        }
        queue.close();
    });

    std::thread consumer([&]() {
        FrameProcessorState state;

        while (true) {
            auto frame_opt = queue.pop();
            if (!frame_opt.has_value()) {
                break;
            }
            if (ProcessFrame(std::move(frame_opt.value()),
                             cfg,
                             context,
                             state,
                             stop_requested,
                             queue)) {
                break;
            }
        }
    });

    producer.join();
    consumer.join();

    logger::Info("处理完成。");
    return 0;
}
