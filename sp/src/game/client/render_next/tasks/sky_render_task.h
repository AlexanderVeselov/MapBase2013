#pragma once

#include "render_graph.h"

#include "../gpu_scene_resources.h"

#include "gpu_descriptor_set.hpp"
#include "gpu_pipeline.hpp"
#include "gpu_sampler.hpp"

#include <array>

class SkyRenderTask final : public RenderTask
{
public:
    void Initialize(gpu::DevicePtr const& device, RenderBackendResources const& backend_resources, RenderSceneGpu const& gpu_scene);
    void LoadSky(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
        std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, RenderBackendResources const& backend_resources,
        RenderSceneGpu const& gpu_scene, char const* sky_name);
    char const* GetName() const override;
    void Execute(RenderTaskContext& context) override;

private:
    void UpdateBindings(RenderBackendResources const& backend_resources, RenderSceneGpu const& gpu_scene);

private:
    gpu::ComputePipelinePtr pipeline_;
    gpu::DescriptorSetPtr descriptor_set_;
    gpu::SamplerPtr sky_sampler_;
    std::array<gpu::ImagePtr, 6> sky_faces_ = {};
};

