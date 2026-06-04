#pragma once

#include "render_scene.h"

inline uint32_t AddRenderInstance(std::vector<RenderInstance>& out_instances, std::vector<Vertex>& out_vertices, uint32_t first_vertex,
    uint32_t vertex_count, uint32_t material_index, uint32_t transform_index)
{
    RenderInstance instance = {};
    instance.first_vertex = first_vertex;
    instance.vertex_count = vertex_count;
    instance.material_index = material_index;
    instance.transform_index = transform_index;

    uint32_t instance_id = static_cast<uint32_t>(out_instances.size());
    out_instances.push_back(instance);
    for (uint32_t vertex_offset = 0; vertex_offset < vertex_count; ++vertex_offset)
    {
        out_vertices[first_vertex + vertex_offset].instance_id = instance_id;
    }

    return instance_id;
}
