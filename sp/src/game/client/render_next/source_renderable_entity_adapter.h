#pragma once

#include "render_scene.h"
#include "source_scene_cache.h"

#include <unordered_set>
#include <vector>

class SourceRenderableEntityAdapter
{
public:
    void Reset();
    void UpdateRenderableEntities(RenderSceneCpu& scene, SourceSceneBuildCache const& build_cache);

private:
    struct RenderableEntity
    {
        int entity_index = -1;
        int submodel_index = 0;
        uint32_t transform_index = 0;
        uint32_t first_instance = 0;
        uint32_t instance_count = 0;
    };

private:
    std::vector<RenderableEntity> renderable_entities_;
    std::unordered_set<int> logged_studio_entities_;
};
