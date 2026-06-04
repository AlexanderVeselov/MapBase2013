#pragma once

#include "render_scene.h"

#include <vector>

struct SourceSceneBuildCache
{
    std::vector<SceneTransform> base_transforms;
    std::vector<RenderInstance> base_instances;
    std::vector<BrushModelSourceRange> brush_model_ranges;
};
