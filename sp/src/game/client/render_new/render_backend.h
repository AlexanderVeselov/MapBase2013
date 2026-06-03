#pragma once

#include "render_scene.h"

#include "gpu_api.hpp"
#include "gpu_buffer.hpp"
#include "gpu_command_buffer.hpp"
#include "gpu_descriptor_set.hpp"
#include "gpu_device.hpp"
#include "gpu_image.hpp"
#include "gpu_pipeline.hpp"
#include "gpu_queue.hpp"
#include "gpu_sampler.hpp"

#include <memory>
#include <string>
#include <unordered_map>

struct RenderBackendContext
{
    std::unique_ptr<gpu::Api> api;
    gpu::DevicePtr device;
    gpu::Queue* graphics_queue = nullptr;
    gpu::CommandBufferPtr cmd_buffer;
    std::unordered_map<gpu::Image*, gpu::ImageLayout> image_layouts;
    std::string shader_dir;
};

struct RenderBackendResources
{
    gpu::ImagePtr color_texture;
    gpu::ImagePtr depth_texture;
    gpu::ImagePtr shared_depth_texture;
    gpu::BufferPtr view_proj_buffer;
    gpu::SamplerPtr texture_sampler;
    gpu::SamplerPtr lightmap_sampler;
};

void InitializeRenderBackend(char const* source_file_path, RenderBackendContext& context,
    RenderBackendResources& resources, RenderSceneGpu& gpu_scene);
void EnsureRenderCommandBuffer(RenderBackendContext& context);
void TransitionRenderImage(RenderBackendContext& context, gpu::ImagePtr const& image, gpu::ImageLayout desired_layout);
void SubmitRenderCommandsAndWait(RenderBackendContext& context);
