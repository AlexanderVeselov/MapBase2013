#pragma once

#include "engine_adapter.h"
#include "scene_builder.h"

#include <vector>

class SourceAdapter final : public EngineAdapter
{
public:
    char const* GetLevelName() const override;
    char const* GetSkyName() const override;
    void GetSkyboxTextureNames(std::array<std::string, 6>& out_texture_names) const override;

    void BuildWorldScene(char const* level_name, RenderSceneCpu& out_scene) override;
    void InitializeBrushEntities(RenderSceneCpu& scene) override;
    void UpdateDynamicSceneTransforms(RenderSceneCpu& scene) override;

private:
    struct DynamicTransformBinding
    {
        int entity_index = -1;
        uint32_t transform_index = 0;
    };

private:
    SourceSceneBuildCache build_cache_;
    std::vector<DynamicTransformBinding> dynamic_bindings_;
    bool brush_entities_initialized_ = false;
};
