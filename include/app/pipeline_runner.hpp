#pragma once

#include "config.hpp"
#include "app/pipeline_callbacks.hpp"

int RunPipeline(const AppConfig& cfg, const PipelineCallbacks* callbacks = nullptr);
