#pragma once

#include "render_graph.h"
#include "../source_adapter/source_texture_manager.h"

#include "gpu_buffer.hpp"
#include "gpu_descriptor_set.hpp"
#include "gpu_pipeline.hpp"
#include "gpu_sampler.hpp"
#include "mathlib/vmatrix.h"

class RenderShadowmapsTask final : public RenderTask
{
public:
    void Initialize(gpu::DevicePtr const& device, RenderScene const& scene, SourceTextureManager const& texture_manager);
    void UpdateSceneBindings(RenderScene const& scene, SourceTextureManager const& texture_manager);
    char const* GetName() const override;
    void Execute(RenderTaskContext& context) override;

private:
    bool BuildDirectionalShadowMatrix(std::array<Vector, 8> const& frustum_corners, SceneLight const& light,
        VMatrix& out_world_to_shadow) const;

private:
    gpu::GraphicsPipelinePtr pipeline_;
    gpu::DescriptorSetPtr descriptor_set_;
    gpu::SamplerPtr texture_sampler_;
    gpu::BufferPtr shadow_camera_buffer_;
    gpu::BufferPtr shadow_camera_staging_buffer_;
};
