#pragma once

#include "mathlib/mathlib.h"
#include <vector>

// Pack data
struct Vertex
{
    Vector pos;
    Vector color;
    float uv[2];
};

void LoadBsp(char const* filename, std::vector<Vertex>& out_vertices);
