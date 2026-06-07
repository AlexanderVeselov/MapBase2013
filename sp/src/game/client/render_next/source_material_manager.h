#pragma once

#include "texture_manager.h"

#include "gpu_command_buffer.hpp"
#include "gpu_device.hpp"
#include "gpu_image.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct Material
{
    uint32_t albedo_texture_id = 0;
};

class SourceMaterialManager
{
public:
    uint32_t LoadMaterial(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
        std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, TextureManager& texture_manager,
        char const* material_name);

    Material const& GetMaterial(uint32_t material_id) const;

private:
    void EnsureFallbackMaterial();

private:
    std::unordered_map<std::string, uint32_t> material_ids_by_name_;
    std::vector<Material> materials_;
};
