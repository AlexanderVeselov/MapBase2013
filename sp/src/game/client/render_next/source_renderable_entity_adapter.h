#pragma once

#include "render_scene.h"
#include "source_scene_cache.h"

#include <vector>

class SourceRenderableEntityAdapter
{
public:
    void Reset();
    void PopulateRenderableEntities(RenderSceneCpu& scene, SourceSceneBuildCache const& build_cache);
    void UpdateRenderableEntities(RenderSceneCpu& scene);

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
    void SetRenderableEntityVisibility(RenderSceneCpu& scene, RenderableEntity const& renderable_entity, uint32_t visibility);

private:
    std::vector<RenderableEntity> renderable_entities_;
    bool brush_entities_initialized_ = false;
};
