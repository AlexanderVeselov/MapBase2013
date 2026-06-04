#include "cbase.h"
#include "source_static_prop_loader.h"

#include "bone_setup.h"
#include "bsp_loader.h"
#include "datacache/imdlcache.h"
#include "engine/ivmodelinfo.h"
#include "istudiorender.h"
#include "source_scene_utils.h"

#include <algorithm>
#include <string>
#include <unordered_map>

namespace
{
void ComputeStaticPropVertexColor(Vertex const& source_vertex, matrix3x4_t const& model_to_world, float out_color[3])
{
    Vector world_position;
    Vector world_normal;
    VectorTransform(source_vertex.pos, model_to_world, world_position);
    VectorRotate(source_vertex.normal, model_to_world, world_normal);
    if (world_normal.Dot(world_normal) > 0.0f)
    {
        world_normal.NormalizeInPlace();
    }

    Vector lighting(1.0f, 1.0f, 1.0f);
    if (engine)
    {
        engine->ComputeLighting(world_position, &world_normal, true, lighting);
    }

    out_color[0] = lighting.x;
    out_color[1] = lighting.y;
    out_color[2] = lighting.z;
}

uint32_t FindOrAddMaterial(std::unordered_map<std::string, uint32_t>& material_indices,
    std::vector<RenderMaterial>& materials_out, std::string const& material_name)
{
    if (material_name.empty())
    {
        return 0;
    }

    auto existing = material_indices.find(material_name);
    if (existing != material_indices.end())
    {
        return existing->second;
    }

    materials_out.push_back(RenderMaterial{material_name, 1, 1});
    uint32_t material_index = static_cast<uint32_t>(materials_out.size());
    material_indices.emplace(material_name, material_index);
    return material_index;
}

Vertex MakeStaticPropVertex(GetTriangles_Vertex_t const& source_vertex, float fallback_lightmap_u, float fallback_lightmap_v)
{
    Vertex vertex = {source_vertex.m_Position, source_vertex.m_Normal, {source_vertex.m_TexCoord.x, source_vertex.m_TexCoord.y},
        {fallback_lightmap_u, fallback_lightmap_v}};
    return vertex;
}

struct StaticPropMeshRange
{
    uint32_t vertex_offset = 0;
    uint32_t vertex_count = 0;
    uint32_t index_offset = 0;
    uint32_t index_count = 0;
    uint32_t material_index = 0;
};

using StaticPropMeshKey = std::string;

uint32_t AppendStaticPropVertexColors(std::vector<Vertex> const& vertices, uint32_t first_vertex, uint32_t vertex_count,
    matrix3x4_t const& model_to_world, std::vector<VertexColorData>& out_vertex_colors)
{
    uint32_t vertex_color_offset = static_cast<uint32_t>(out_vertex_colors.size());
    out_vertex_colors.reserve(out_vertex_colors.size() + vertex_count);
    for (uint32_t vertex_offset = 0; vertex_offset < vertex_count; ++vertex_offset)
    {
        VertexColorData vertex_color = {};
        ComputeStaticPropVertexColor(vertices[first_vertex + vertex_offset], model_to_world, vertex_color.color);
        out_vertex_colors.push_back(vertex_color);
    }

    return vertex_color_offset;
}
}

void SourceStaticPropLoader::AppendStaticProps(char const* level_name, RenderSceneCpu& io_scene) const
{
    if (!modelinfo || !mdlcache || !g_pStudioRender)
    {
        return;
    }

    std::vector<StaticPropInstance> static_props;
    LoadStaticProps(level_name, static_props);
    if (static_props.empty())
    {
        return;
    }

    std::unordered_map<std::string, uint32_t> material_indices;
    material_indices.reserve(io_scene.materials.size());
    for (size_t material_index = 0; material_index < io_scene.materials.size(); ++material_index)
    {
        material_indices.emplace(io_scene.materials[material_index].material_name, static_cast<uint32_t>(material_index + 1));
    }

    float fallback_lightmap_u = 0.5f / static_cast<float>((std::max)(io_scene.lightmap_atlas.width, 1));
    float fallback_lightmap_v = 0.5f / static_cast<float>((std::max)(io_scene.lightmap_atlas.height, 1));
    int appended_prop_count = 0;
    int appended_triangle_count = 0;
    std::unordered_map<StaticPropMeshKey, std::vector<StaticPropMeshRange>> mesh_ranges_by_key;

    MDLCACHE_CRITICAL_SECTION();
    for (StaticPropInstance const& static_prop : static_props)
    {
        matrix3x4_t model_to_world;
        AngleMatrix(static_prop.angles, static_prop.origin, model_to_world);
        uint32_t transform_index = static_cast<uint32_t>(io_scene.transforms.size());
        io_scene.transforms.push_back(MakeSceneTransform(model_to_world));

        StaticPropMeshKey mesh_key = static_prop.model_name + "#" + std::to_string(static_prop.skin);
        auto existing_mesh = mesh_ranges_by_key.find(mesh_key);
        if (existing_mesh != mesh_ranges_by_key.end())
        {
            for (StaticPropMeshRange const& mesh_range : existing_mesh->second)
            {
                uint32_t vertex_color_offset = AppendStaticPropVertexColors(io_scene.vertices, mesh_range.vertex_offset, mesh_range.vertex_count,
                    model_to_world, io_scene.vertex_colors);
                AddRenderInstance(io_scene.instances, mesh_range.vertex_offset, mesh_range.index_offset, mesh_range.index_count,
                    mesh_range.material_index, transform_index, nullptr, vertex_color_offset);
            }

            ++appended_prop_count;
            continue;
        }

        MDLHandle_t mdl_handle = mdlcache->FindMDL(static_prop.model_name.c_str());
        if (mdl_handle == MDLHANDLE_INVALID)
        {
            continue;
        }

        studiohdr_t* studio_hdr = mdlcache->LockStudioHdr(mdl_handle);
        if (!studio_hdr)
        {
            continue;
        }

        studiohwdata_t* hardware_data = mdlcache->GetHardwareData(mdl_handle);
        if (!hardware_data)
        {
            mdlcache->UnlockStudioHdr(mdl_handle);
            continue;
        }

        CStudioHdr studio_hdr_wrapper(studio_hdr, mdlcache);
        float pose_parameters[MAXSTUDIOPOSEPARAM] = {};
        Vector bone_positions[MAXSTUDIOBONES];
        Quaternion bone_rotations[MAXSTUDIOBONES];
        matrix3x4_t bone_to_world[MAXSTUDIOBONES];

        IBoneSetup bone_setup(&studio_hdr_wrapper, BONE_USED_BY_ANYTHING, pose_parameters);
        bone_setup.InitPose(bone_positions, bone_rotations);
        Studio_BuildMatrices(&studio_hdr_wrapper, static_prop.angles, static_prop.origin, bone_positions, bone_rotations, -1, 1.0f,
            bone_to_world, BONE_USED_BY_ANYTHING);

        DrawModelInfo_t draw_info = {};
        draw_info.m_pStudioHdr = studio_hdr;
        draw_info.m_pHardwareData = hardware_data;
        draw_info.m_Skin = static_prop.skin;
        draw_info.m_Body = 0;
        draw_info.m_HitboxSet = 0;
        draw_info.m_pClientEntity = nullptr;
        draw_info.m_Lod = hardware_data->m_RootLOD;
        draw_info.m_bStaticLighting = true;

        GetTriangles_Output_t triangle_output;
        g_pStudioRender->GetTriangles(draw_info, bone_to_world, triangle_output);
        std::vector<StaticPropMeshRange> mesh_ranges;
        for (int batch_index = 0; batch_index < triangle_output.m_MaterialBatches.Count(); ++batch_index)
        {
            GetTriangles_MaterialBatch_t const& material_batch = triangle_output.m_MaterialBatches[batch_index];
            std::string material_name = material_batch.m_pMaterial ? material_batch.m_pMaterial->GetName() : "";
            uint32_t material_index = FindOrAddMaterial(material_indices, io_scene.materials, material_name);
            uint32_t vertex_offset = static_cast<uint32_t>(io_scene.vertices.size());
            uint32_t index_offset = static_cast<uint32_t>(io_scene.indices.size());
            int triangles_before_batch = appended_triangle_count;

            auto append_vertex_by_index = [&](int vertex_index) -> uint32_t
            {
                if (vertex_index < 0 || vertex_index >= material_batch.m_Verts.Count())
                {
                    return 0;
                }

                Vertex vertex = MakeStaticPropVertex(material_batch.m_Verts[vertex_index], fallback_lightmap_u, fallback_lightmap_v);
                io_scene.vertices.push_back(vertex);
                return static_cast<uint32_t>(io_scene.vertices.size() - 1 - vertex_offset);
            };

            if (material_batch.m_TriListIndices.Count() >= 3)
            {
                for (int index = 0; index + 2 < material_batch.m_TriListIndices.Count(); index += 3)
                {
                    io_scene.indices.push_back(append_vertex_by_index(material_batch.m_TriListIndices[index + 0]));
                    io_scene.indices.push_back(append_vertex_by_index(material_batch.m_TriListIndices[index + 1]));
                    io_scene.indices.push_back(append_vertex_by_index(material_batch.m_TriListIndices[index + 2]));
                    ++appended_triangle_count;
                }
            }
            else
            {
                for (int vertex_index = 0; vertex_index + 2 < material_batch.m_Verts.Count(); vertex_index += 3)
                {
                    io_scene.indices.push_back(append_vertex_by_index(vertex_index + 0));
                    io_scene.indices.push_back(append_vertex_by_index(vertex_index + 1));
                    io_scene.indices.push_back(append_vertex_by_index(vertex_index + 2));
                    ++appended_triangle_count;
                }
            }

            uint32_t vertex_count = static_cast<uint32_t>(io_scene.vertices.size()) - vertex_offset;
            uint32_t index_count = static_cast<uint32_t>(io_scene.indices.size()) - index_offset;
            if (vertex_count > 0 && index_count > 0 && appended_triangle_count > triangles_before_batch)
            {
                mesh_ranges.push_back({vertex_offset, vertex_count, index_offset, index_count, material_index});
                uint32_t vertex_color_offset = AppendStaticPropVertexColors(io_scene.vertices, vertex_offset, vertex_count,
                    model_to_world, io_scene.vertex_colors);
                AddRenderInstance(io_scene.instances, vertex_offset, index_offset, index_count, material_index, transform_index,
                    nullptr, vertex_color_offset);
            }
        }

        if (!mesh_ranges.empty())
        {
            mesh_ranges_by_key.emplace(std::move(mesh_key), std::move(mesh_ranges));
            ++appended_prop_count;
        }

        mdlcache->UnlockStudioHdr(mdl_handle);
    }

    Msg("render_next: static props instances=%d rendered=%d triangles=%d\n",
        static_props.size(), appended_prop_count, appended_triangle_count);
}
