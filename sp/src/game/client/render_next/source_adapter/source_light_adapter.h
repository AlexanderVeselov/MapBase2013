#pragma once

struct RenderScene;

class SourceLightAdapter
{
public:
    void LoadWorldLights(char const* level_name, RenderScene& io_scene) const;
};
