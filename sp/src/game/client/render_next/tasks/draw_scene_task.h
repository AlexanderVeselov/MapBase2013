#pragma once

#include "render_graph.h"
#include "../source_adapter/source_texture_manager.h"

#include "gpu_descriptor_set.hpp"
#include "gpu_pipeline.hpp"
#include "gpu_sampler.hpp"

class DrawSceneTask final : public RenderTask
{
public:
    void Initialize(gpu::DevicePtr const& device, gpu::BufferPtr const& view_proj_buffer, RenderScene const& scene);
    void UpdateSceneBindings(gpu::BufferPtr const& view_proj_buffer, RenderScene const& scene, SourceTextureManager const& texture_manager);
    char const* GetName() const override;
    void Execute(RenderTaskContext& context) override;

private:
    gpu::GraphicsPipelinePtr pipeline_;
    gpu::DescriptorSetPtr descriptor_set_;
    gpu::SamplerPtr texture_sampler_;
    gpu::SamplerPtr lightmap_sampler_;
};

