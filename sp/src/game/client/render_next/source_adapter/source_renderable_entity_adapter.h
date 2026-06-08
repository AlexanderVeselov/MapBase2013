#pragma once

#include "model_types.h"
#include "../render_scene.h"
#include "source_model_manager.h"
#include "source_scene_cache.h"

class SourceRenderableEntityAdapter
{
public:
    void UpdateRenderableEntities(RenderScene& scene, SourceSceneBuildCache const& build_cache, SourceModelManager& model_manager,
        gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
        std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, SourceTextureManager& texture_manager,
        SourceMaterialManager& material_manager);
};
