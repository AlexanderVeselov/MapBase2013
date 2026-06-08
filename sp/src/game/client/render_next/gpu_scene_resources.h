#pragma once

#include "render_scene.h"
#include "source_adapter/source_material_manager.h"
#include "source_adapter/source_texture_manager.h"

#include "gpu_buffer.hpp"
#include "gpu_command_buffer.hpp"
#include "gpu_device.hpp"
#include "gpu_image.hpp"

#include <cstdint>
#include <array>
#include <unordered_map>

std::vector<uint32_t> BuildMaterialIds(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, RenderScene const& scene,
    SourceTextureManager& texture_manager, SourceMaterialManager& material_manager);

std::vector<Material> BuildUploadedMaterials(std::vector<uint32_t> const& material_ids,
    SourceMaterialManager const& material_manager);

void UploadSkyboxTexturesToGpu(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, std::array<std::string, 6> const& skybox_texture_names,
    SourceTextureManager& texture_manager, RenderScene& out_scene);

void SyncRenderSceneToGpu(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, RenderScene& scene,
    SourceTextureManager& texture_manager, SourceMaterialManager& material_manager);

