#pragma once

#include <memory>

class CameraProvider;
class CalibrationMapper;
class OnnxObbInferencer;
class SubpixelCaliper;
class ObbTracker;
class FrameVisualizer;
class SpatialErrorCompensator;

struct PipelineContext {
    PipelineContext();
    ~PipelineContext();
    PipelineContext(PipelineContext&&) noexcept;
    PipelineContext& operator=(PipelineContext&&) noexcept;
    PipelineContext(const PipelineContext&) = delete;
    PipelineContext& operator=(const PipelineContext&) = delete;

    std::unique_ptr<CameraProvider> camera;
    std::unique_ptr<CalibrationMapper> calibration;
    std::unique_ptr<OnnxObbInferencer> inferencer;
    std::unique_ptr<SubpixelCaliper> caliper;
    std::unique_ptr<ObbTracker> tracker;
    std::unique_ptr<FrameVisualizer> visualizer;
    std::unique_ptr<SpatialErrorCompensator> compensator;
};
