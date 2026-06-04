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
    uint32_t texture_index;
    float color[3] = {1.0f, 1.0f, 1.0f};
    uint32_t transform_index = 0;
};

struct BspMaterial
{
    std::string material_name;
    int width = 1;
    int height = 1;
};

struct BspLightmapAtlas
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

struct BrushSubmodel
{
    int submodel_index = 0;
    uint32_t first_vertex = 0;
    uint32_t vertex_count = 0;
};

void LoadBsp(char const* filename, std::vector<Vertex>& out_vertices, std::vector<BspMaterial>& out_materials,
    BspLightmapAtlas& out_lightmap_atlas, std::vector<BrushSubmodel>& out_brush_submodels);
void LoadStaticProps(char const* filename, std::vector<StaticPropInstance>& out_static_props);

