#pragma once

#include "model_types.h"
#include "../render_scene.h"
#include "source_scene_cache.h"

class SourceRenderableEntityAdapter
{
public:
    void UpdateRenderableEntities(RenderScene& scene, SourceSceneBuildCache const& build_cache);
};
