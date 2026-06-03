#pragma once

#include "render_scene.h"

#include "gpu_buffer.hpp"
#include "gpu_command_buffer.hpp"
#include "gpu_descriptor_set.hpp"
#include "gpu_device.hpp"
#include "gpu_image.hpp"
#include "gpu_sampler.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

void UploadRenderSceneToGpu(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, RenderSceneCpu const& scene,
    gpu::BufferPtr const& view_proj_buffer, gpu::SamplerPtr const& texture_sampler, gpu::SamplerPtr const& lightmap_sampler,
    gpu::ImagePtr const& fallback_texture, gpu::ImagePtr const& fallback_lightmap_texture,
    gpu::DescriptorSetPtr const& pipeline_descriptor_set, RenderSceneGpu& out_gpu_scene);
