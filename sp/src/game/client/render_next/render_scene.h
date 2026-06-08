#pragma once

#include "bsp_loader.h"
#include "mirrored_buffer.h"
#include "source_adapter/source_material_manager.h"

#include "gpu_command_buffer.hpp"
#include "gpu_image.hpp"

#include <cstdint>
#include <array>
#include <unordered_map>
#include <vector>

struct SceneTransform
{
    float m[4][4] = {};
};

inline SceneTransform MakeIdentitySceneTransform()
{
    SceneTransform transform = {};
    transform.m[0][0] = 1.0f;
    transform.m[1][1] = 1.0f;
    transform.m[2][2] = 1.0f;
    transform.m[3][3] = 1.0f;
    return transform;
}

inline SceneTransform MakeSceneTransform(matrix3x4_t const& source_transform)
{
    SceneTransform transform = {};

    transform.m[0][0] = source_transform[0][0];
    transform.m[0][1] = source_transform[1][0];
    transform.m[0][2] = source_transform[2][0];
    transform.m[0][3] = 0.0f;

    transform.m[1][0] = source_transform[0][1];
    transform.m[1][1] = source_transform[1][1];
    transform.m[1][2] = source_transform[2][1];
    transform.m[1][3] = 0.0f;

    transform.m[2][0] = source_transform[0][2];
    transform.m[2][1] = source_transform[1][2];
    transform.m[2][2] = source_transform[2][2];
    transform.m[2][3] = 0.0f;

    transform.m[3][0] = source_transform[0][3];
    transform.m[3][1] = source_transform[1][3];
    transform.m[3][2] = source_transform[2][3];
    transform.m[3][3] = 1.0f;

    return transform;
}

struct RenderInstance
{
    static constexpr uint32_t kInvalidVertexColorOffset = UINT32_MAX;
    static constexpr uint32_t kInvalidBoneOffset = UINT32_MAX;
    static constexpr uint32_t kVisible = 1;
    static constexpr uint32_t kHidden = 0;

    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    uint32_t vertex_offset = 0;
    uint32_t index_offset = 0;
    uint32_t index_count = 0;
    uint32_t material_index = 0;
    uint32_t transform_index = 0;
    uint32_t vertex_color_offset = kInvalidVertexColorOffset;
    uint32_t bone_offset = kInvalidBoneOffset;
    uint32_t bone_count = 0;
    uint32_t is_visible = kVisible;
    uint32_t padding0 = 0;
};

static_assert(sizeof(RenderInstance) == 56, "RenderInstance must match HLSL InstanceData layout");

struct VertexColorData
{
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

struct SceneBoneMatrix
{
    float m[4][4] = {};
};

inline SceneBoneMatrix MakeSceneBoneMatrix(matrix3x4_t const& source_transform)
{
    SceneBoneMatrix bone_matrix = {};

    bone_matrix.m[0][0] = source_transform[0][0];
    bone_matrix.m[0][1] = source_transform[1][0];
    bone_matrix.m[0][2] = source_transform[2][0];
    bone_matrix.m[0][3] = 0.0f;

    bone_matrix.m[1][0] = source_transform[0][1];
    bone_matrix.m[1][1] = source_transform[1][1];
    bone_matrix.m[1][2] = source_transform[2][1];
    bone_matrix.m[1][3] = 0.0f;

    bone_matrix.m[2][0] = source_transform[0][2];
    bone_matrix.m[2][1] = source_transform[1][2];
    bone_matrix.m[2][2] = source_transform[2][2];
    bone_matrix.m[2][3] = 0.0f;

    bone_matrix.m[3][0] = source_transform[0][3];
    bone_matrix.m[3][1] = source_transform[1][3];
    bone_matrix.m[3][2] = source_transform[2][3];
    bone_matrix.m[3][3] = 1.0f;

    return bone_matrix;
}

struct RenderScene
{
    MirroredBuffer<Vertex> vertices;
    MirroredBuffer<uint32_t> indices;
    MirroredBuffer<SceneTransform> transforms{gpu::BufferFlags::kShaderResource};
    MirroredBuffer<SceneBoneMatrix> bones{gpu::BufferFlags::kShaderResource};
    MirroredBuffer<VertexColorData> vertex_colors{gpu::BufferFlags::kShaderResource};
    MirroredBuffer<RenderInstance> instances{gpu::BufferFlags::kShaderResource};
    MirroredBuffer<Material> materials{gpu::BufferFlags::kShaderResource};
    LightmapAtlas lightmap_atlas;
    gpu::ImagePtr fallback_lightmap_texture;
    gpu::ImagePtr lightmap_texture;
    std::array<uint32_t, 6> skybox_texture_ids = {};

    void Reset()
    {
        vertices.Reset();
        indices.Reset();
        transforms.Reset();
        bones.Reset();
        vertex_colors.Reset();
        instances.Reset();
        materials.Reset();
        lightmap_atlas = {};
        fallback_lightmap_texture.reset();
        lightmap_texture.reset();
        skybox_texture_ids.fill(0);
    }

    void EnsureFallbackTextures(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
        std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts);
};
