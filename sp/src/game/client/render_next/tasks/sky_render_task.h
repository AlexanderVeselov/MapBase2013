#pragma once

#include "render_graph.h"
#include "../texture_manager.h"

#include "../gpu_scene_resources.h"

#include "gpu_descriptor_set.hpp"
#include "gpu_pipeline.hpp"
#include "gpu_sampler.hpp"

class SkyRenderTask final : public RenderTask
{
public:
    void Initialize(gpu::DevicePtr const& device, RenderBackendResources const& backend_resources, RenderSceneGpu const& gpu_scene,
        TextureManager const& texture_manager);
    void UpdateBindings(RenderBackendResources const& backend_resources, RenderSceneGpu const& gpu_scene, TextureManager const& texture_manager);
    char const* GetName() const override;
    void Execute(RenderTaskContext& context) override;

private:
    gpu::ComputePipelinePtr pipeline_;
    gpu::DescriptorSetPtr descriptor_set_;
    gpu::SamplerPtr sky_sampler_;
};

