#pragma once

#include "render_graph.h"

#include "gpu_descriptor_set.hpp"
#include "gpu_pipeline.hpp"

class DrawSceneTask final : public RenderTask
{
public:
    void Initialize(gpu::DevicePtr const& device, gpu::BufferPtr const& view_proj_buffer,
        gpu::SamplerPtr const& texture_sampler, gpu::SamplerPtr const& lightmap_sampler, RenderSceneGpu const& gpu_scene);
    void UpdateSceneBindings(gpu::BufferPtr const& view_proj_buffer, gpu::SamplerPtr const& texture_sampler,
        gpu::SamplerPtr const& lightmap_sampler, RenderSceneGpu const& gpu_scene);
    char const* GetName() const override;
    void Execute(RenderTaskContext& context) override;

private:
    gpu::GraphicsPipelinePtr pipeline_;
    gpu::DescriptorSetPtr descriptor_set_;
};
