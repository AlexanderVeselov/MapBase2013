#pragma once

#include "render_scene.h"
#include "source_scene_cache.h"

#include <vector>

class SourceBrushEntityAdapter
{
public:
    void Reset();
    void InitializeBrushEntities(RenderSceneCpu& scene, SourceSceneBuildCache const& build_cache);
    void UpdateDynamicSceneTransforms(RenderSceneCpu& scene);

private:
    struct DynamicTransformBinding
    {
        int entity_index = -1;
        uint32_t transform_index = 0;
    };

private:
    std::vector<DynamicTransformBinding> dynamic_bindings_;
    bool brush_entities_initialized_ = false;
};
