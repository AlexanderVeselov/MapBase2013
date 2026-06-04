#pragma once

#include "engine_adapter.h"
#include "source_brush_entity_adapter.h"
#include "source_scene_cache.h"
#include "source_static_prop_loader.h"
#include "source_world_loader.h"

#include <array>
#include <string>
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
    SourceBrushEntityAdapter brush_entity_adapter_;
    SourceWorldLoader world_loader_;
    SourceStaticPropLoader static_prop_loader_;
    SourceSceneBuildCache build_cache_;
};
