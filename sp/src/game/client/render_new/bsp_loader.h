#pragma once

#include "mathlib/mathlib.h"
#include <vector>

struct Vertex
{
    Vector pos;
    Vector color;
};

void LoadBsp(char const* filename, std::vector<Vertex>& out_vertices);
