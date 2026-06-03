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
    uint32_t texture_index;
};

struct BspMaterial
{
    std::string material_name;
    int width = 1;
    int height = 1;
};

void LoadBsp(char const* filename, std::vector<Vertex>& out_vertices, std::vector<BspMaterial>& out_materials);
