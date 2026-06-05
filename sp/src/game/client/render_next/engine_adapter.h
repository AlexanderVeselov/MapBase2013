#pragma once

#include <array>
#include <string>

struct RenderSceneCpu;

class EngineAdapter
{
public:
    virtual ~EngineAdapter() = default;

    virtual char const* GetLevelName() const = 0;
    virtual char const* GetSkyName() const = 0;
    virtual void GetSkyboxTextureNames(std::array<std::string, 6>& out_texture_names) const = 0;

    virtual void BuildWorldScene(char const* level_name, RenderSceneCpu& out_scene) = 0;
    virtual void UpdateRenderableEntities(RenderSceneCpu& scene) = 0;
};
