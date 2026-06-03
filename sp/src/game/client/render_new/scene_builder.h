#pragma once

#include "render_scene.h"

void BuildRenderSceneCpu(char const* level_name, RenderSceneCpu& out_scene);
void InitializeBrushEntities(RenderSceneCpu& scene);
