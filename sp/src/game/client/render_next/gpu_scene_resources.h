#pragma once

#include "render_scene.h"

#include "gpu_buffer.hpp"
#include "gpu_command_buffer.hpp"
#include "gpu_device.hpp"
#include "gpu_image.hpp"

#include <cstdint>
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

gpu::ImagePtr LoadTextureImage(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, char const* texture_name);

void UploadSkyboxTexturesToGpu(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, std::array<std::string, 6> const& skybox_texture_names,
    RenderSceneGpu& out_gpu_scene);

void UploadRenderSceneToGpu(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, RenderSceneCpu const& scene, RenderSceneGpu& out_gpu_scene);

