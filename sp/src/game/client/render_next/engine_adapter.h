#pragma once

#include "gpu_command_buffer.hpp"
#include "gpu_device.hpp"
#include "gpu_image.hpp"
#include "source_adapter/source_material_manager.h"
#include "source_adapter/source_texture_manager.h"

#include <array>
#include <string>
#include <unordered_map>

struct RenderScene;

class EngineAdapter
{
public:
    virtual ~EngineAdapter() = default;

    virtual char const* GetLevelName() const = 0;
    virtual char const* GetSkyName() const = 0;
    virtual void GetSkyboxTextureNames(std::array<std::string, 6>& out_texture_names) const = 0;

    virtual void BuildWorldScene(char const* level_name, RenderScene& out_scene, gpu::DevicePtr const& device,
        gpu::CommandBuffer& cmd_buffer, std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts,
        SourceTextureManager& texture_manager, SourceMaterialManager& material_manager) = 0;
    virtual void UpdateRenderableEntities(RenderScene& scene) = 0;
};
