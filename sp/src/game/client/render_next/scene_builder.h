#pragma once

#include "render_scene.h"

struct SourceSceneBuildCache
{
    std::vector<Vertex> base_vertices;
    std::vector<SceneTransform> base_transforms;
    std::vector<RenderInstance> base_instances;
    std::vector<Vertex> brush_model_vertices;
    std::vector<BrushModelSourceRange> brush_model_ranges;
};

void BuildRenderSceneCpu(char const* level_name, RenderSceneCpu& out_scene, SourceSceneBuildCache& out_cache);

