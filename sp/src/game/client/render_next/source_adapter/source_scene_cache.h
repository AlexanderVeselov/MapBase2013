#pragma once

#include "../render_scene.h"

#include <vector>

struct SourceSceneBuildCache
{
    std::vector<SceneTransform> static_transforms;
    std::vector<VertexColorData> static_vertex_colors;
    std::vector<AmbientCubeColorData> static_ambient_cubes;
    std::vector<RenderInstance> static_instances;
    std::vector<BrushModelSourceRange> brush_model_ranges;
};
