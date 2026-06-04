#include "cbase.h"
#include "source_world_loader.h"

#include "bsp_loader.h"
#include "source_scene_utils.h"

void SourceWorldLoader::BuildBaseScene(char const* level_name, RenderSceneCpu& out_scene, SourceSceneBuildCache& out_cache) const
{
    out_scene = {};
    out_cache = {};
    out_scene.transforms.push_back(MakeIdentitySceneTransform());

    LoadBsp(level_name, out_cache.brush_model_vertices, out_scene.materials, out_scene.lightmap_atlas, out_cache.brush_model_ranges);
    out_scene.vertices = out_cache.brush_model_vertices;
    if (!out_cache.brush_model_ranges.empty())
    {
        out_scene.vertices.resize(out_cache.brush_model_ranges.front().first_vertex);
    }

    uint32_t vertex_offset = 0;
    while (vertex_offset < out_scene.vertices.size())
    {
        uint32_t range_end = vertex_offset + 1;
        uint32_t material_index = out_scene.vertices[vertex_offset].instance_id;
        while (range_end < out_scene.vertices.size() && out_scene.vertices[range_end].instance_id == material_index)
        {
            ++range_end;
        }

        AddRenderInstance(out_scene.instances, out_scene.vertices, vertex_offset, range_end - vertex_offset, material_index, 0);
        vertex_offset = range_end;
    }
}
