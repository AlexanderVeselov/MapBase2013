#include "cbase.h"
#include "source_world_loader.h"

#include "bsp_loader.h"
#include "source_scene_utils.h"

void SourceWorldLoader::BuildBaseScene(char const* level_name, RenderSceneCpu& out_scene, SourceSceneBuildCache& out_cache) const
{
    out_scene = {};
    out_cache = {};
    out_scene.transforms.push_back(MakeIdentitySceneTransform());

    std::vector<MeshSourceRange> world_mesh_ranges;
    LoadBsp(level_name, out_scene.vertices, out_scene.materials, out_scene.lightmap_atlas, world_mesh_ranges, out_cache.brush_model_ranges);

    for (MeshSourceRange const& mesh_range : world_mesh_ranges)
    {
        if (mesh_range.vertex_count == 0)
        {
            continue;
        }

        AddRenderInstance(out_scene.instances, mesh_range.first_vertex, mesh_range.vertex_count, mesh_range.material_index, 0);
    }
}
