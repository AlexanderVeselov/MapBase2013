#pragma once

#include "source_texture_manager.h"

#include "gpu_command_buffer.hpp"
#include "gpu_device.hpp"
#include "gpu_image.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

struct RenderScene;

struct Material
{
    uint32_t albedo_texture_id = 0;
    uint32_t alpha_test = 0;
    float alpha_test_reference = 0.5f;
    float padding0 = 0.0f;
};

class SourceMaterialManager
{
public:
    void Reset();

    uint32_t LoadMaterial(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
        std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, RenderScene& scene,
        SourceTextureManager& texture_manager, char const* material_name);

private:
    void EnsureFallbackMaterial(RenderScene& scene);

private:
    std::unordered_map<std::string, uint32_t> material_ids_by_name_;
};
