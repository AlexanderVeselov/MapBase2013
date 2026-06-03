#pragma once

#include "bsp_loader.h"

#include "gpu_buffer.hpp"
#include "gpu_command_buffer.hpp"
#include "gpu_image.hpp"

#include <cstdint>
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

struct BrushEntityInstance
{
    int entity_index = -1;
    uint32_t transform_index = 0;
};

struct RenderSceneCpu
{
    std::vector<SceneTransform> transforms;
    std::vector<Vertex> vertices;
    std::vector<Vertex> base_vertices;
    std::vector<Vertex> brush_source_vertices;
    std::vector<BspMaterial> materials;
    std::vector<BrushSubmodel> brush_submodels;
    std::vector<BrushEntityInstance> brush_entities;
    BspLightmapAtlas lightmap_atlas;
    bool brush_entities_initialized = false;
};

struct RenderSceneGpu
{
    gpu::BufferPtr vertex_buffer;
    gpu::BufferPtr scene_transform_buffer;
    gpu::ImagePtr fallback_texture;
    gpu::ImagePtr fallback_lightmap_texture;
    gpu::ImagePtr lightmap_texture;
    std::vector<gpu::ImagePtr> material_textures;
    uint32_t vertex_count = 0;

    void EnsureFallbackTextures(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
        std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts);
};
