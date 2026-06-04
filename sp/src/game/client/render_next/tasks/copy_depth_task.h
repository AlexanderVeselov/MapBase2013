#pragma once

#include "render_graph.h"

#include "gpu_descriptor_set.hpp"
#include "gpu_pipeline.hpp"

class CopyDepthTask final : public RenderTask
{
public:
    void Initialize(gpu::DevicePtr const& device, RenderBackendResources const& backend_resources);
    char const* GetName() const override;
    void Execute(RenderTaskContext& context) override;

private:
    gpu::ComputePipelinePtr pipeline_;
    gpu::DescriptorSetPtr descriptor_set_;
};

