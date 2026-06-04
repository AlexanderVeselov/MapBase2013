#pragma once

struct RenderSceneCpu;

class EngineAdapter
{
public:
    virtual ~EngineAdapter() = default;

    virtual char const* GetLevelName() const = 0;
    virtual char const* GetSkyName() const = 0;

    virtual void BuildWorldScene(char const* level_name, RenderSceneCpu& out_scene) = 0;
    virtual void InitializeBrushEntities(RenderSceneCpu& scene) = 0;
    virtual void UpdateDynamicSceneTransforms(RenderSceneCpu& scene) = 0;
};
