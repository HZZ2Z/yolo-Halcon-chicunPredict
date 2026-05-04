#include "app/pipeline_context.hpp"

#include "app/frame_visualizer.hpp"
#include "calibration.hpp"
#include "camera_provider.hpp"
#include "onnx_inferencer.hpp"
#include "spatial_error_compensator.hpp"
#include "subpixel_caliper.hpp"
#include "tracker_ekf.hpp"

PipelineContext::PipelineContext() = default;
PipelineContext::~PipelineContext() {
    visualizer.reset();
    tracker.reset();
    caliper.reset();
    compensator.reset();
    inferencer.reset();
    calibration.reset();
    camera.reset();
}
PipelineContext::PipelineContext(PipelineContext&&) noexcept = default;
PipelineContext& PipelineContext::operator=(PipelineContext&&) noexcept = default;
