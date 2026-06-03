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
};

struct BspMaterial
{
    std::string material_name;
    int width = 1;
    int height = 1;
};

struct BspLightmapAtlas
{
    int width = 1;
    int height = 1;
    std::vector<uint8_t> rgba_pixels;
};

struct StaticPropInstance
{
    std::string model_name;
    Vector origin;
    QAngle angles;
    int skin = 0;
};

void LoadBsp(char const* filename, std::vector<Vertex>& out_vertices, std::vector<BspMaterial>& out_materials,
    BspLightmapAtlas& out_lightmap_atlas);
void LoadStaticProps(char const* filename, std::vector<StaticPropInstance>& out_static_props);
