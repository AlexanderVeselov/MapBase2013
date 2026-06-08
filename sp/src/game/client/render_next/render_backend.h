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
    gpu::ImagePtr velocity_texture;
    gpu::ImagePtr depth_texture;
    gpu::ImagePtr shared_depth_texture;
    gpu::BufferPtr camera_buffer;
    gpu::BufferPtr camera_staging_buffer;
    gpu::BufferPtr inverse_view_proj_buffer;
    gpu::BufferPtr inverse_view_proj_staging_buffer;
};

void InitializeRenderBackend(char const* source_file_path, RenderBackendContext& context,
    RenderBackendResources& resources);
void EnsureRenderCommandBuffer(RenderBackendContext& context);
void TransitionRenderImage(RenderBackendContext& context, gpu::ImagePtr const& image, gpu::ImageLayout desired_layout);
void SubmitRenderCommandsAndWait(RenderBackendContext& context);
void UploadBufferData(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer, gpu::BufferPtr& staging_buffer,
    gpu::BufferPtr const& dst_buffer, void const* data, size_t data_size);
