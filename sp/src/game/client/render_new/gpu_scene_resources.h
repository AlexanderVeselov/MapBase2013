#pragma once

#include "render_scene.h"

#include "gpu_buffer.hpp"
#include "gpu_command_buffer.hpp"
#include "gpu_device.hpp"
#include "gpu_image.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

void UploadRenderSceneToGpu(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, RenderSceneCpu const& scene, RenderSceneGpu& out_gpu_scene);
