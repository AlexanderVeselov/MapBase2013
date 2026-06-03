#pragma once

#include "render_backend.h"
#include "render_scene.h"

#include <vector>

struct RenderTaskContext
{
    RenderBackendContext& backend;
    RenderBackendResources& backend_resources;
    RenderSceneGpu& gpu_scene;
    uint32_t viewport_width = 0;
    uint32_t viewport_height = 0;
};

class RenderTask
{
public:
    virtual ~RenderTask() = default;
    virtual char const* GetName() const = 0;
    virtual void Execute(RenderTaskContext& context) = 0;
};

class RenderGraph
{
public:
    void Reset();
    void AddTask(RenderTask& task);
    void Execute(RenderTaskContext& context) const;

private:
    std::vector<RenderTask*> tasks_;
};
