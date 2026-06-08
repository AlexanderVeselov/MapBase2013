#pragma once

#include "source_scene_cache.h"

class SourceWorldLoader
{
public:
    void BuildBaseScene(char const* level_name, RenderScene& out_scene, SourceSceneBuildCache& out_cache) const;
};
