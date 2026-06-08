#include "cbase.h"
#include "source_model_manager.h"

#include "bone_setup.h"
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

uint32_t FindOrAddMaterial(std::unordered_map<std::string, uint32_t>& material_indices, std::string const& material_name,
    RenderScene& scene, gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, SourceTextureManager& texture_manager,
    SourceMaterialManager& material_manager)
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

    uint32_t material_index = material_manager.LoadMaterial(device, cmd_buffer, image_layouts, scene, texture_manager,
        material_name.c_str());
    material_indices.emplace(material_name, material_index);
    return material_index;
}

Vertex MakeStaticPropVertex(GetTriangles_Vertex_t const& source_vertex, float fallback_lightmap_u, float fallback_lightmap_v)
{
    Vertex vertex = {source_vertex.m_Position, source_vertex.m_Normal, {source_vertex.m_TexCoord.x, source_vertex.m_TexCoord.y},
        {fallback_lightmap_u, fallback_lightmap_v}};
    return vertex;
}

uint32_t AppendStaticPropVertexColors(MirroredBuffer<Vertex> const& vertices, uint32_t first_vertex, uint32_t vertex_count,
    matrix3x4_t const& model_to_world, MirroredBuffer<VertexColorData>& out_vertex_colors)
{
    uint32_t vertex_color_offset = out_vertex_colors.Size();
    for (uint32_t vertex_offset = 0; vertex_offset < vertex_count; ++vertex_offset)
    {
        VertexColorData vertex_color = {};
        ComputeStaticPropVertexColor(vertices[first_vertex + vertex_offset], model_to_world, vertex_color.color);
        out_vertex_colors.Append(vertex_color);
    }

    return vertex_color_offset;
}

void ApplyFallbackLightmapUvs(std::vector<Vertex>& io_vertices, RenderScene const& scene)
{
    float fallback_lightmap_u = 0.5f / static_cast<float>((std::max)(scene.lightmap_atlas.width, 1));
    float fallback_lightmap_v = 0.5f / static_cast<float>((std::max)(scene.lightmap_atlas.height, 1));

    for (Vertex& vertex : io_vertices)
    {
        vertex.lightmap_uv[0] = fallback_lightmap_u;
        vertex.lightmap_uv[1] = fallback_lightmap_v;
    }
}

std::string BuildModelCacheKey(char const* model_name, int skin)
{
    return std::string(model_name ? model_name : "") + "#" + std::to_string(skin);
}

bool AppendInstanceRanges(std::vector<RenderInstance> const& cached_instances, matrix3x4_t const& model_to_world,
    RenderScene& io_scene)
{
    if (cached_instances.empty())
    {
        return false;
    }

    uint32_t transform_index = io_scene.transforms.Append(MakeSceneTransform(model_to_world)).offset;

    for (RenderInstance const& cached_instance : cached_instances)
    {
        uint32_t vertex_color_offset = AppendStaticPropVertexColors(io_scene.vertices, cached_instance.vertex_offset,
            cached_instance.padding0,
            model_to_world, io_scene.vertex_colors);

        RenderInstance instance = cached_instance;
        instance.transform_index = transform_index;
        instance.vertex_color_offset = vertex_color_offset;
        instance.is_visible = RenderInstance::kVisible;
        io_scene.instances.Append(instance);
    }

    return true;
}
}

void SourceModelManager::Reset()
{
    material_indices_.clear();
    model_instances_by_key_.clear();
}

bool SourceModelManager::AppendLoadedModelGeometry(char const* model_name, int skin, RenderScene& io_scene,
    gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, SourceTextureManager& texture_manager,
    SourceMaterialManager& material_manager, std::vector<RenderInstance>& out_cached_instances)
{
    if (!model_name || model_name[0] == '\0' || !mdlcache || !g_pStudioRender)
    {
        return false;
    }

    MDLHandle_t mdl_handle = mdlcache->FindMDL(model_name);
    if (mdl_handle == MDLHANDLE_INVALID)
    {
        return false;
    }

    MDLCACHE_CRITICAL_SECTION();
    studiohdr_t* studio_hdr = mdlcache->LockStudioHdr(mdl_handle);
    if (!studio_hdr)
    {
        return false;
    }

    studiohwdata_t* hardware_data = mdlcache->GetHardwareData(mdl_handle);
    if (!hardware_data)
    {
        mdlcache->UnlockStudioHdr(mdl_handle);
        return false;
    }

    CStudioHdr studio_hdr_wrapper(studio_hdr, mdlcache);
    float pose_parameters[MAXSTUDIOPOSEPARAM] = {};
    Vector bone_positions[MAXSTUDIOBONES];
    Quaternion bone_rotations[MAXSTUDIOBONES];
    matrix3x4_t bone_to_world[MAXSTUDIOBONES];

    IBoneSetup bone_setup(&studio_hdr_wrapper, BONE_USED_BY_ANYTHING, pose_parameters);
    bone_setup.InitPose(bone_positions, bone_rotations);

    QAngle identity_angles(0.0f, 0.0f, 0.0f);
    Vector identity_origin(0.0f, 0.0f, 0.0f);
    Studio_BuildMatrices(&studio_hdr_wrapper, identity_angles, identity_origin, bone_positions, bone_rotations, -1, 1.0f,
        bone_to_world, BONE_USED_BY_ANYTHING);

    DrawModelInfo_t draw_info = {};
    draw_info.m_pStudioHdr = studio_hdr;
    draw_info.m_pHardwareData = hardware_data;
    draw_info.m_Skin = skin;
    draw_info.m_Body = 0;
    draw_info.m_HitboxSet = 0;
    draw_info.m_pClientEntity = nullptr;
    draw_info.m_Lod = hardware_data->m_RootLOD;
    draw_info.m_bStaticLighting = true;

    GetTriangles_Output_t triangle_output;
    g_pStudioRender->GetTriangles(draw_info, bone_to_world, triangle_output);

    out_cached_instances.clear();
    out_cached_instances.reserve(triangle_output.m_MaterialBatches.Count());
    for (int batch_index = 0; batch_index < triangle_output.m_MaterialBatches.Count(); ++batch_index)
    {
        GetTriangles_MaterialBatch_t const& material_batch = triangle_output.m_MaterialBatches[batch_index];
        std::vector<Vertex> mesh_vertices;
        std::vector<uint32_t> mesh_indices;
        int triangles_before_batch = 0;

        auto append_vertex_by_index = [&](int vertex_index) -> uint32_t
        {
            if (vertex_index < 0 || vertex_index >= material_batch.m_Verts.Count())
            {
                return 0;
            }

            Vertex vertex = MakeStaticPropVertex(material_batch.m_Verts[vertex_index], 0.0f, 0.0f);
            mesh_vertices.push_back(vertex);
            return static_cast<uint32_t>(mesh_vertices.size() - 1);
        };

        if (material_batch.m_TriListIndices.Count() >= 3)
        {
            for (int index = 0; index + 2 < material_batch.m_TriListIndices.Count(); index += 3)
            {
                mesh_indices.push_back(append_vertex_by_index(material_batch.m_TriListIndices[index + 0]));
                mesh_indices.push_back(append_vertex_by_index(material_batch.m_TriListIndices[index + 1]));
                mesh_indices.push_back(append_vertex_by_index(material_batch.m_TriListIndices[index + 2]));
                ++triangles_before_batch;
            }
        }
        else
        {
            for (int vertex_index = 0; vertex_index + 2 < material_batch.m_Verts.Count(); vertex_index += 3)
            {
                mesh_indices.push_back(append_vertex_by_index(vertex_index + 0));
                mesh_indices.push_back(append_vertex_by_index(vertex_index + 1));
                mesh_indices.push_back(append_vertex_by_index(vertex_index + 2));
                ++triangles_before_batch;
            }
        }

        uint32_t material_index = FindOrAddMaterial(material_indices_,
            material_batch.m_pMaterial ? material_batch.m_pMaterial->GetName() : "", io_scene, device, cmd_buffer,
            image_layouts, texture_manager, material_manager);
        if (!mesh_vertices.empty() && !mesh_indices.empty() && triangles_before_batch > 0)
        {
            ApplyFallbackLightmapUvs(mesh_vertices, io_scene);
            MirroredBuffer<Vertex>::Slice vertex_slice = io_scene.vertices.Append(mesh_vertices);
            MirroredBuffer<uint32_t>::Slice index_slice = io_scene.indices.Append(mesh_indices);
            RenderInstance instance = {};
            instance.vertex_offset = vertex_slice.offset;
            instance.index_offset = index_slice.offset;
            instance.index_count = index_slice.count;
            instance.material_index = material_index;
            instance.transform_index = 0;
            instance.vertex_color_offset = RenderInstance::kInvalidVertexColorOffset;
            instance.is_visible = RenderInstance::kVisible;
            instance.padding0 = vertex_slice.count;
            out_cached_instances.push_back(instance);
        }
    }

    mdlcache->UnlockStudioHdr(mdl_handle);
    return !out_cached_instances.empty();
}

bool SourceModelManager::LoadModel(char const* model_name, int skin, matrix3x4_t const& model_to_world, RenderScene& io_scene,
    gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, SourceTextureManager& texture_manager,
    SourceMaterialManager& material_manager)
{
    std::string model_key = BuildModelCacheKey(model_name, skin);
    auto existing_instance = model_instances_by_key_.find(model_key);
    if (existing_instance != model_instances_by_key_.end())
    {
        return AppendInstanceRanges(existing_instance->second, model_to_world, io_scene);
    }

    std::vector<RenderInstance> cached_instances;
    if (!AppendLoadedModelGeometry(model_name, skin, io_scene, device, cmd_buffer, image_layouts,
            texture_manager, material_manager, cached_instances))
    {
        return false;
    }

    model_instances_by_key_.emplace(model_key, cached_instances);

    return AppendInstanceRanges(cached_instances, model_to_world, io_scene);
}
