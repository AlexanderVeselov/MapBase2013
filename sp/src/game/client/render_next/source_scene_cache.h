#pragma once

#include "render_scene.h"

#include <vector>

struct SourceSceneBuildCache
{
    std::vector<Vertex> base_vertices;
    std::vector<SceneTransform> base_transforms;
    std::vector<RenderInstance> base_instances;
    std::vector<Vertex> brush_model_vertices;
    std::vector<BrushModelSourceRange> brush_model_ranges;
};
