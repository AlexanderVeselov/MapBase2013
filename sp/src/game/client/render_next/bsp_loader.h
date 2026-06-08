#pragma once

#include "mathlib/mathlib.h"
#include "mirrored_buffer.h"
#include "source_adapter/source_material_manager.h"
#include "source_adapter/source_texture_manager.h"

#include "gpu_command_buffer.hpp"
#include "gpu_device.hpp"
#include "gpu_image.hpp"

#include <unordered_map>
#include <cstdint>
#include <string>
#include <vector>

struct RenderScene;
class SourceModelManager;

struct Vertex
{
    Vector pos;
    Vector normal;
    float uv[2];
    float lightmap_uv[2];
};

struct MeshSourceRange
{
    uint32_t first_vertex = 0;
    uint32_t first_index = 0;
    uint32_t index_count = 0;
    uint32_t material_index = 0;
};

struct LightmapAtlas
{
    enum class Format
    {
        kRGBA8,
        kRGBA32Float
    };

    int width = 1;
    int height = 1;
    Format format = Format::kRGBA8;
    std::vector<uint8_t> pixels;
};

struct StaticPropInstance
{
    std::string model_name;
    Vector origin;
    QAngle angles;
    int skin = 0;
};

struct BrushModelSourceRange
{
    int submodel_index = 0;
    uint32_t first_vertex = 0;
    uint32_t first_index = 0;
    uint32_t index_count = 0;
    uint32_t material_index = 0;
};

void LoadBsp(char const* filename, RenderScene& io_scene, gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, SourceTextureManager& texture_manager,
    SourceMaterialManager& material_manager, SourceModelManager& model_manager, LightmapAtlas& out_lightmap_atlas,
    std::vector<BrushModelSourceRange>& out_brush_model_ranges);
void LoadStaticProps(char const* filename, std::vector<StaticPropInstance>& out_static_props);
