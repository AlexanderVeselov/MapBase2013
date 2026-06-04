#pragma once

#include "render_scene.h"

inline uint32_t AddRenderInstance(std::vector<RenderInstance>& out_instances, uint32_t first_vertex, uint32_t vertex_count,
    uint32_t material_index, uint32_t transform_index, float const* color = nullptr,
    uint32_t vertex_color_offset = RenderInstance::kInvalidVertexColorOffset)
{
    RenderInstance instance = {};
    instance.first_vertex = first_vertex;
    instance.vertex_count = vertex_count;
    instance.material_index = material_index;
    instance.transform_index = transform_index;
    instance.vertex_color_offset = vertex_color_offset;
    if (color)
    {
        instance.color[0] = color[0];
        instance.color[1] = color[1];
        instance.color[2] = color[2];
    }

    uint32_t instance_id = static_cast<uint32_t>(out_instances.size());
    out_instances.push_back(instance);
    return instance_id;
}
