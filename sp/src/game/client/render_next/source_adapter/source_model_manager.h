#pragma once

#include "../render_scene.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class SourceModelManager
{
public:
    void Reset();

    bool LoadModel(char const* model_name, int skin, matrix3x4_t const& model_to_world, RenderScene& io_scene,
        gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
        std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, SourceTextureManager& texture_manager,
        SourceMaterialManager& material_manager);

private:
    bool AppendLoadedModelGeometry(char const* model_name, int skin, RenderScene& io_scene,
        gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
        std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, SourceTextureManager& texture_manager,
        SourceMaterialManager& material_manager, std::vector<RenderInstance>& out_cached_instances);

private:
    std::unordered_map<std::string, uint32_t> material_indices_;
    std::unordered_map<std::string, std::vector<RenderInstance>> model_instances_by_key_;
};
