#pragma once

#include "mathlib/mathlib.h"
#include <cstdint>
#include <string>
#include <vector>

struct Vertex
{
    Vector pos;
    Vector normal;
    float uv[2];
    float lightmap_uv[2];
    uint32_t instance_id = 0;
    float color[3] = {1.0f, 1.0f, 1.0f};
};

struct RenderMaterial
{
    std::string material_name;
    int width = 1;
    int height = 1;
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
    uint32_t vertex_count = 0;
    uint32_t material_index = 0;
};

void LoadBsp(char const* filename, std::vector<Vertex>& out_vertices, std::vector<RenderMaterial>& out_materials,
    LightmapAtlas& out_lightmap_atlas, std::vector<BrushModelSourceRange>& out_brush_model_ranges);
void LoadStaticProps(char const* filename, std::vector<StaticPropInstance>& out_static_props);

