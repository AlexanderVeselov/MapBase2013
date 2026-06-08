#pragma once

#include "../render_scene.h"

inline uint32_t AddRenderInstance(MirroredBuffer<RenderInstance>& out_instances, uint32_t vertex_offset, uint32_t index_offset,
    uint32_t index_count, uint32_t material_index, uint32_t transform_index, float const* color = nullptr,
    uint32_t vertex_color_offset = RenderInstance::kInvalidVertexColorOffset, uint32_t vertex_count = 0,
    uint32_t bone_offset = RenderInstance::kInvalidBoneOffset, uint32_t bone_count = 0)
{
    RenderInstance instance = {};
    instance.vertex_offset = vertex_offset;
    instance.index_offset = index_offset;
    instance.index_count = index_count;
    instance.material_index = material_index;
    instance.transform_index = transform_index;
    instance.vertex_color_offset = vertex_color_offset;
    instance.bone_offset = bone_offset;
    instance.bone_count = bone_count;
    instance.is_visible = RenderInstance::kVisible;
    instance.padding0 = vertex_count;
    if (color)
    {
        instance.color[0] = color[0];
        instance.color[1] = color[1];
        instance.color[2] = color[2];
    }

    return out_instances.Append(instance).offset;
}
