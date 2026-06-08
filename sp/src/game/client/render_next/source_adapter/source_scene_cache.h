#pragma once

#include "../render_scene.h"

#include <vector>

struct SourceSceneBuildCache
{
    std::vector<SceneTransform> static_transforms;
    std::vector<RenderInstance> static_instances;
    std::vector<BrushModelSourceRange> brush_model_ranges;
};
