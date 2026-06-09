#pragma once

#include "../engine_adapter.h"
#include "source_light_adapter.h"
#include "source_model_manager.h"
#include "source_renderable_entity_adapter.h"
#include "source_scene_cache.h"

#include <array>
#include <string>
#include <vector>

class SourceAdapter final : public EngineAdapter
{
public:
    char const* GetLevelName() const override;
    char const* GetSkyName() const override;
    void GetSkyboxTextureNames(std::array<std::string, 6>& out_texture_names) const override;

    void BuildWorldScene(char const* level_name, RenderScene& out_scene, gpu::DevicePtr const& device,
        gpu::CommandBuffer& cmd_buffer, std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts,
        SourceTextureManager& texture_manager, SourceMaterialManager& material_manager) override;
    void UpdateRenderableEntities(RenderScene& scene, gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
        std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, SourceTextureManager& texture_manager,
        SourceMaterialManager& material_manager) override;

    SourceRenderableEntityAdapter renderable_entity_adapter_;
    SourceLightAdapter light_adapter_;
    SourceModelManager model_manager_;
    SourceSceneBuildCache build_cache_;
};
