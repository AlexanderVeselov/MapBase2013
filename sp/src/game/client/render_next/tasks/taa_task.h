#pragma once

#include "render_graph.h"

#include "gpu_descriptor_set.hpp"
#include "gpu_pipeline.hpp"
#include "gpu_sampler.hpp"

class TaaTask final : public RenderTask
{
public:
    void Initialize(gpu::DevicePtr const& device);
    void ResetHistory();
    char const* GetName() const override;
    void Execute(RenderTaskContext& context) override;

private:
    void EnsureHistoryTexture(RenderTaskContext& context);

private:
    gpu::Device* device_ = nullptr;
    gpu::ImagePtr history_texture_;
    bool has_valid_history_ = false;
    gpu::ComputePipelinePtr pipeline_;
    gpu::DescriptorSetPtr descriptor_set_;
    gpu::SamplerPtr history_sampler_;
};