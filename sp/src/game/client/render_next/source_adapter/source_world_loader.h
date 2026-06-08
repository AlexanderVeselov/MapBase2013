#pragma once

#include "source_scene_cache.h"
#include "source_material_manager.h"
#include "source_texture_manager.h"

#include <unordered_map>

#include "source_material_manager.h"
#include "source_texture_manager.h"

class SourceWorldLoader
{
public:
    void BuildBaseScene(char const* level_name, RenderScene& out_scene, SourceSceneBuildCache& out_cache,
        gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
        std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, SourceTextureManager& texture_manager,
        SourceMaterialManager& material_manager) const;
};
