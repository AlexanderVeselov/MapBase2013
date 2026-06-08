#pragma once

#include "gpu_command_buffer.hpp"
#include "gpu_descriptor_set.hpp"
#include "gpu_device.hpp"
#include "gpu_image.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class SourceTextureManager
{
public:
    void Reset();

    uint32_t LoadTexture(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
        std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, char const* texture_name);

    uint32_t GetTextureCount() const;
    gpu::ImagePtr const& GetTexture(uint32_t texture_id) const;
    void BuildDescriptorArray(uint32_t count, std::vector<gpu::ImageDescriptor>& out_descriptors) const;

private:
    void EnsureFallbackTexture(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
        std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts);

private:
    std::unordered_map<std::string, uint32_t> texture_ids_by_name_;
    std::vector<gpu::ImagePtr> textures_;
};
