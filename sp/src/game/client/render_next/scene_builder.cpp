#include "cbase.h"
#include "scene_builder.h"

#include "bone_setup.h"
#include "cdll_client_int.h"
#include "cliententitylist.h"
#include "icliententity.h"

#include "datacache/imdlcache.h"
#include "engine/ivmodelinfo.h"
#include "istudiorender.h"
#include "materialsystem/imaterial.h"
#include "model_types.h"

#include <algorithm>
#include <cfloat>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace
{
void ComputeVertexBounds(std::vector<Vertex> const& vertices, uint32_t first_vertex, uint32_t vertex_count, Vector& mins, Vector& maxs)
{
    mins.Init(FLT_MAX, FLT_MAX, FLT_MAX);
    maxs.Init(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    if (first_vertex >= vertices.size() || vertex_count == 0 || first_vertex + vertex_count > vertices.size())
    {
        mins.Init();
        maxs.Init();
        return;
    }

    for (uint32_t vertex_offset = 0; vertex_offset < vertex_count; ++vertex_offset)
    {
        Vector const& position = vertices[first_vertex + vertex_offset].pos;
        mins.x = (std::min)(mins.x, position.x);
        mins.y = (std::min)(mins.y, position.y);
        mins.z = (std::min)(mins.z, position.z);
        maxs.x = (std::max)(maxs.x, position.x);
        maxs.y = (std::max)(maxs.y, position.y);
        maxs.z = (std::max)(maxs.z, position.z);
    }
}

bool TryParseBrushSubmodelIndex(char const* model_name, int& out_submodel_index)
{
    if (!model_name || model_name[0] != '*')
    {
        return false;
    }

    char* parse_end = nullptr;
    long parsed_value = std::strtol(model_name + 1, &parse_end, 10);
    if (parse_end == model_name + 1 || !parse_end || *parse_end != '\0' || parsed_value <= 0)
    {
        return false;
    }

    out_submodel_index = static_cast<int>(parsed_value);
    return true;
}

void ComputeStaticPropVertexColor(GetTriangles_Vertex_t const& source_vertex, matrix3x4_t const& model_to_world, float out_color[3])
{
    Vector world_position;
    Vector world_normal;
    VectorTransform(source_vertex.m_Position, model_to_world, world_position);
    VectorRotate(source_vertex.m_Normal, model_to_world, world_normal);
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
    std::vector<BspMaterial>& materials_out, std::string const& material_name)
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

    materials_out.push_back(BspMaterial{material_name, 1, 1});
    uint32_t texture_index = static_cast<uint32_t>(materials_out.size());
    material_indices.emplace(material_name, texture_index);
    return texture_index;
}

Vertex MakeStaticPropVertex(GetTriangles_Vertex_t const& source_vertex, uint32_t transform_index,
    matrix3x4_t const& model_to_world, matrix3x4_t const pose_to_world[MAXSTUDIOBONES],
    uint32_t texture_index, float fallback_lightmap_u, float fallback_lightmap_v)
{
    (void)pose_to_world;

    Vertex vertex = {source_vertex.m_Position, source_vertex.m_Normal, {source_vertex.m_TexCoord.x, source_vertex.m_TexCoord.y},
        {fallback_lightmap_u, fallback_lightmap_v}, texture_index};
    ComputeStaticPropVertexColor(source_vertex, model_to_world, vertex.color);
    vertex.transform_index = transform_index;
    return vertex;
}

void AppendStaticPropTriangles(char const* level_name, std::vector<Vertex>& out_vertices, std::vector<BspMaterial>& out_materials,
    BspLightmapAtlas const& lightmap_atlas, std::vector<SceneTransform>& out_scene_transforms)
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
    material_indices.reserve(out_materials.size());
    for (size_t material_index = 0; material_index < out_materials.size(); ++material_index)
    {
        material_indices.emplace(out_materials[material_index].material_name, static_cast<uint32_t>(material_index + 1));
    }

    float fallback_lightmap_u = 0.5f / static_cast<float>((std::max)(lightmap_atlas.width, 1));
    float fallback_lightmap_v = 0.5f / static_cast<float>((std::max)(lightmap_atlas.height, 1));
    int appended_prop_count = 0;
    int appended_triangle_count = 0;

    MDLCACHE_CRITICAL_SECTION();
    for (StaticPropInstance const& static_prop : static_props)
    {
        matrix3x4_t model_to_world;
        AngleMatrix(static_prop.angles, static_prop.origin, model_to_world);
        uint32_t transform_index = static_cast<uint32_t>(out_scene_transforms.size());
        out_scene_transforms.push_back(MakeSceneTransform(model_to_world));

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
        int triangles_before_prop = appended_triangle_count;

        for (int batch_index = 0; batch_index < triangle_output.m_MaterialBatches.Count(); ++batch_index)
        {
            GetTriangles_MaterialBatch_t const& material_batch = triangle_output.m_MaterialBatches[batch_index];
            std::string material_name = material_batch.m_pMaterial ? material_batch.m_pMaterial->GetName() : "";
            uint32_t texture_index = FindOrAddMaterial(material_indices, out_materials, material_name);

            auto append_vertex_by_index = [&](int vertex_index)
            {
                if (vertex_index < 0 || vertex_index >= material_batch.m_Verts.Count())
                {
                    return;
                }

                out_vertices.push_back(MakeStaticPropVertex(material_batch.m_Verts[vertex_index], transform_index, model_to_world,
                    triangle_output.m_PoseToWorld, texture_index, fallback_lightmap_u, fallback_lightmap_v));
            };

            if (material_batch.m_TriListIndices.Count() >= 3)
            {
                for (int index = 0; index + 2 < material_batch.m_TriListIndices.Count(); index += 3)
                {
                    append_vertex_by_index(material_batch.m_TriListIndices[index + 0]);
                    append_vertex_by_index(material_batch.m_TriListIndices[index + 1]);
                    append_vertex_by_index(material_batch.m_TriListIndices[index + 2]);
                    ++appended_triangle_count;
                }
            }
            else
            {
                for (int vertex_index = 0; vertex_index + 2 < material_batch.m_Verts.Count(); vertex_index += 3)
                {
                    append_vertex_by_index(vertex_index + 0);
                    append_vertex_by_index(vertex_index + 1);
                    append_vertex_by_index(vertex_index + 2);
                    ++appended_triangle_count;
                }
            }
        }

        if (appended_triangle_count > triangles_before_prop)
        {
            ++appended_prop_count;
        }

        mdlcache->UnlockStudioHdr(mdl_handle);
    }

    Msg("render_next: static props instances=%d rendered=%d triangles=%d\n",
        static_props.size(), appended_prop_count, appended_triangle_count);
}
}

void BuildRenderSceneCpu(char const* level_name, RenderSceneCpu& out_scene)
{
    out_scene = {};
    out_scene.transforms.push_back(MakeIdentitySceneTransform());

    LoadBsp(level_name, out_scene.vertices, out_scene.materials, out_scene.lightmap_atlas, out_scene.brush_submodels);
    out_scene.brush_source_vertices = out_scene.vertices;
    if (!out_scene.brush_submodels.empty())
    {
        out_scene.vertices.resize(out_scene.brush_submodels.front().first_vertex);
    }

    AppendStaticPropTriangles(level_name, out_scene.vertices, out_scene.materials, out_scene.lightmap_atlas, out_scene.transforms);
    out_scene.base_vertices = out_scene.vertices;
}

void InitializeBrushEntities(RenderSceneCpu& scene)
{
    if (scene.brush_entities_initialized || scene.brush_submodels.empty() || !cl_entitylist)
    {
        return;
    }

    int highest_entity_index = cl_entitylist->GetHighestEntityIndex();
    if (highest_entity_index < 0)
    {
        return;
    }

    std::vector<Vertex> rebuilt_vertices = scene.base_vertices;
    std::vector<BrushEntityInstance> brush_entities;
    std::vector<SceneTransform> rebuilt_scene_transforms = scene.transforms;

    brush_entities.clear();
    if (!modelinfo || !cl_entitylist || scene.brush_submodels.empty())
    {
        return;
    }

    std::unordered_map<int, BrushSubmodel const*> brush_submodel_lookup;
    brush_submodel_lookup.reserve(scene.brush_submodels.size());
    for (BrushSubmodel const& brush_submodel : scene.brush_submodels)
    {
        brush_submodel_lookup.emplace(brush_submodel.submodel_index, &brush_submodel);
    }

    int rendered_brush_entities = 0;
    int debug_logged_entities = 0;
    highest_entity_index = cl_entitylist->GetHighestEntityIndex();
    for (int entity_index = 0; entity_index <= highest_entity_index; ++entity_index)
    {
        IClientEntity* entity = cl_entitylist->GetClientEntity(entity_index);
        if (!entity)
        {
            continue;
        }

        IClientRenderable* renderable = entity->GetClientRenderable();
        if (!renderable || renderable->IsTransparent())
        {
            continue;
        }

        model_t const* model = renderable->GetModel();
        if (!model || modelinfo->GetModelType(model) != mod_brush)
        {
            continue;
        }

        char const* model_name = modelinfo->GetModelName(model);
        int submodel_index = 0;
        if (!TryParseBrushSubmodelIndex(model_name, submodel_index))
        {
            continue;
        }

        auto brush_submodel_it = brush_submodel_lookup.find(submodel_index);
        if (brush_submodel_it == brush_submodel_lookup.end())
        {
            continue;
        }

        BrushSubmodel const& brush_submodel = *brush_submodel_it->second;
        if (brush_submodel.first_vertex + brush_submodel.vertex_count > scene.brush_source_vertices.size())
        {
            continue;
        }

        matrix3x4_t model_to_world;
        AngleMatrix(entity->GetAbsAngles(), entity->GetAbsOrigin(), model_to_world);
        uint32_t transform_index = 0;

        Vector bounds_before_mins;
        Vector bounds_before_maxs;
        ComputeVertexBounds(scene.brush_source_vertices, brush_submodel.first_vertex, brush_submodel.vertex_count, bounds_before_mins, bounds_before_maxs);

        for (uint32_t vertex_offset = 0; vertex_offset < brush_submodel.vertex_count; ++vertex_offset)
        {
            Vertex vertex = scene.brush_source_vertices[brush_submodel.first_vertex + vertex_offset];
            Vector world_position;
            Vector world_normal;
            VectorTransform(vertex.pos, model_to_world, world_position);
            VectorRotate(vertex.normal, model_to_world, world_normal);
            vertex.pos = world_position;
            vertex.normal = world_normal;
            if (vertex.normal.Dot(vertex.normal) > 0.0f)
            {
                vertex.normal.NormalizeInPlace();
            }
            vertex.transform_index = transform_index;
            rebuilt_vertices.push_back(vertex);
        }

        ++rendered_brush_entities;

        if (debug_logged_entities < 8)
        {
            Msg("render_next brush[%d]: ent=%d model=%s submodel=%d transform=%u abs_origin=(%.2f %.2f %.2f) abs_angles=(%.2f %.2f %.2f)\n",
                debug_logged_entities, entity_index, model_name ? model_name : "<null>", submodel_index, transform_index,
                entity->GetAbsOrigin().x, entity->GetAbsOrigin().y, entity->GetAbsOrigin().z,
                entity->GetAbsAngles().x, entity->GetAbsAngles().y, entity->GetAbsAngles().z);
            Msg("render_next brush[%d]: bounds local=(%.2f %.2f %.2f)-(%.2f %.2f %.2f)\n",
                debug_logged_entities,
                bounds_before_mins.x, bounds_before_mins.y, bounds_before_mins.z,
                bounds_before_maxs.x, bounds_before_maxs.y, bounds_before_maxs.z);
            ++debug_logged_entities;
        }
    }

    Msg("render_next: brush entities rendered=%d submodels=%d\n", rendered_brush_entities, scene.brush_submodels.size());

    if (rebuilt_vertices.size() == scene.base_vertices.size())
    {
        return;
    }

    scene.vertices = std::move(rebuilt_vertices);
    scene.transforms = std::move(rebuilt_scene_transforms);
    scene.brush_entities = std::move(brush_entities);
    scene.brush_entities_initialized = true;
}

