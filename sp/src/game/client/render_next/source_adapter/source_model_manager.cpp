#include "cbase.h"
#include "source_model_manager.h"

#include "bone_setup.h"
#include "datacache/imdlcache.h"
#include "engine/ivmodelinfo.h"
#include "istudiorender.h"
#include "optimize.h"
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

Vertex MakeStudioMeshVertex(Vector const& position, Vector const& normal, Vector2D const& texcoord,
    float fallback_lightmap_u, float fallback_lightmap_v)
{
    Vertex vertex = {};
    VectorCopy(position, vertex.pos);
    VectorCopy(normal, vertex.normal);
    vertex.uv[0] = texcoord.x;
    vertex.uv[1] = texcoord.y;
    vertex.lightmap_uv[0] = fallback_lightmap_u;
    vertex.lightmap_uv[1] = fallback_lightmap_v;
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

void ComputeInstanceColor(matrix3x4_t const& model_to_world, float out_color[4])
{
    Vector world_position(model_to_world[0][3], model_to_world[1][3], model_to_world[2][3]);
    Vector world_normal(0.0f, 0.0f, 1.0f);
    VectorRotate(world_normal, model_to_world, world_normal);
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
    out_color[3] = 1.0f;
}

bool AppendInstanceRanges(std::vector<RenderInstance> const& cached_instances, matrix3x4_t const& model_to_world,
    bool use_per_vertex_lighting, RenderScene& io_scene)
{
    if (cached_instances.empty())
    {
        return false;
    }

    uint32_t transform_index = io_scene.transforms.Append(MakeSceneTransform(model_to_world)).offset;

    for (RenderInstance const& cached_instance : cached_instances)
    {
        RenderInstance instance = cached_instance;
        instance.transform_index = transform_index;
        instance.is_visible = RenderInstance::kVisible;
        if (use_per_vertex_lighting)
        {
            instance.vertex_color_offset = AppendStaticPropVertexColors(io_scene.vertices, cached_instance.vertex_offset,
                cached_instance.padding0, model_to_world, io_scene.vertex_colors);
        }
        else
        {
            instance.vertex_color_offset = RenderInstance::kInvalidVertexColorOffset;
            ComputeInstanceColor(model_to_world, instance.color);
        }
        io_scene.instances.Append(instance);
    }

    return true;
}

uint32_t ResolveMaterialIndex(studiohdr_t const& studio_hdr, studioloddata_t const& lod_data,
    mstudiomesh_t const& mesh, int skin, RenderScene& scene, gpu::DevicePtr const& device,
    gpu::CommandBuffer& cmd_buffer, std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts,
    SourceTextureManager& texture_manager, SourceMaterialManager& material_manager,
    std::unordered_map<std::string, uint32_t>& material_indices)
{
    int material_slot = mesh.material;
    int num_skin_refs = studio_hdr.numskinref;
    int num_skin_families = studio_hdr.numskinfamilies;
    if (num_skin_refs > 0 && num_skin_families > 0)
    {
        int clamped_skin = (std::max)(0, (std::min)(skin, num_skin_families - 1));
        material_slot = *studio_hdr.pSkinref(clamped_skin * num_skin_refs + mesh.material);
    }

    IMaterial* material = nullptr;
    if (material_slot >= 0 && material_slot < lod_data.numMaterials)
    {
        material = lod_data.ppMaterials[material_slot];
    }

    return FindOrAddMaterial(material_indices, material ? material->GetName() : "", scene, device, cmd_buffer,
        image_layouts, texture_manager, material_manager);
}

void AppendTriangleIndices(std::vector<uint32_t>& io_indices, uint32_t a, uint32_t b, uint32_t c)
{
    if (a == b || b == c || a == c)
    {
        return;
    }

    io_indices.push_back(a);
    io_indices.push_back(b);
    io_indices.push_back(c);
}

void AppendStripAsTriangleList(std::vector<uint32_t>& io_indices, std::vector<uint32_t> const& strip_vertices,
    bool is_triangle_strip)
{
    if (strip_vertices.size() < 3)
    {
        return;
    }

    if (!is_triangle_strip)
    {
        for (size_t index = 0; index + 2 < strip_vertices.size(); index += 3)
        {
            AppendTriangleIndices(io_indices, strip_vertices[index + 0], strip_vertices[index + 1], strip_vertices[index + 2]);
        }
        return;
    }

    for (size_t index = 2; index < strip_vertices.size(); ++index)
    {
        uint32_t a = strip_vertices[index - 2];
        uint32_t b = strip_vertices[index - 1];
        uint32_t c = strip_vertices[index - 0];
        if ((index & 1) == 0)
        {
            AppendTriangleIndices(io_indices, a, b, c);
        }
        else
        {
            AppendTriangleIndices(io_indices, b, a, c);
        }
    }
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

    vertexFileHeader_t* vertex_data = mdlcache->GetVertexData(mdl_handle);
    if (!vertex_data || hardware_data->m_NumLODs <= 0 || !hardware_data->m_pLODs)
    {
        mdlcache->UnlockStudioHdr(mdl_handle);
        return false;
    }

    int lod_index = (std::max)(0, (std::min)(hardware_data->m_RootLOD, hardware_data->m_NumLODs - 1));
    studioloddata_t& lod_data = hardware_data->m_pLODs[lod_index];
    int global_mesh_index = 0;
    out_cached_instances.clear();
    for (int body_part_index = 0; body_part_index < studio_hdr->numbodyparts; ++body_part_index)
    {
        mstudiobodyparts_t* body_part = studio_hdr->pBodypart(body_part_index);
        for (int model_index = 0; model_index < body_part->nummodels; ++model_index)
        {
            mstudiomodel_t* model = body_part->pModel(model_index);
            for (int mesh_index = 0; mesh_index < model->nummeshes; ++mesh_index, ++global_mesh_index)
            {
                if (global_mesh_index >= hardware_data->m_NumStudioMeshes)
                {
                    break;
                }

                mstudiomesh_t* mesh = model->pMesh(mesh_index);
                model->vertexdata.pVertexData = vertex_data->GetVertexData();
                model->vertexdata.pTangentData = vertex_data->GetTangentData();
                mesh->vertexdata.modelvertexdata = &model->vertexdata;
                mstudio_meshvertexdata_t const* mesh_vertex_data = &mesh->vertexdata;
                if (!mesh_vertex_data->modelvertexdata || !mesh_vertex_data->modelvertexdata->pVertexData)
                {
                    continue;
                }

                studiomeshdata_t& mesh_data = lod_data.m_pMeshData[global_mesh_index];
                if (mesh_data.m_NumGroup <= 0 || !mesh_data.m_pMeshGroup)
                {
                    continue;
                }

                float fallback_lightmap_u = 0.5f / static_cast<float>((std::max)(io_scene.lightmap_atlas.width, 1));
                float fallback_lightmap_v = 0.5f / static_cast<float>((std::max)(io_scene.lightmap_atlas.height, 1));
                std::vector<Vertex> mesh_vertices;
                std::vector<uint32_t> mesh_indices;

                for (int group_index = 0; group_index < mesh_data.m_NumGroup; ++group_index)
                {
                    studiomeshgroup_t const& mesh_group = mesh_data.m_pMeshGroup[group_index];
                    for (int strip_index = 0; strip_index < mesh_group.m_NumStrips; ++strip_index)
                    {
                        OptimizedModel::StripHeader_t const& strip = mesh_group.m_pStripData[strip_index];
                        std::vector<uint32_t> strip_vertices;
                        strip_vertices.reserve(strip.numIndices);
                        for (int index_in_strip = 0; index_in_strip < strip.numIndices; ++index_in_strip)
                        {
                            int group_index_offset = strip.indexOffset + index_in_strip;
                            int mesh_vertex_index = mesh_group.MeshIndex(group_index_offset);
                            if (mesh_vertex_index < 0 || mesh_vertex_index >= mesh->numvertices)
                            {
                                continue;
                            }

                            Vector const& position = *mesh_vertex_data->Position(mesh_vertex_index);
                            Vector const& normal = *mesh_vertex_data->Normal(mesh_vertex_index);
                            Vector2D const& texcoord = *mesh_vertex_data->Texcoord(mesh_vertex_index);
                            mesh_vertices.push_back(MakeStudioMeshVertex(position, normal, texcoord,
                                fallback_lightmap_u, fallback_lightmap_v));
                            strip_vertices.push_back(static_cast<uint32_t>(mesh_vertices.size() - 1));
                        }

                        bool is_triangle_strip = (strip.flags & OptimizedModel::STRIP_IS_TRISTRIP) != 0;
                        AppendStripAsTriangleList(mesh_indices, strip_vertices, is_triangle_strip);
                    }
                }

                if (mesh_vertices.empty() || mesh_indices.empty())
                {
                    continue;
                }

                uint32_t material_index = ResolveMaterialIndex(*studio_hdr, lod_data, *mesh, skin, io_scene, device,
                    cmd_buffer, image_layouts, texture_manager, material_manager, material_indices_);
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
    }

    mdlcache->UnlockStudioHdr(mdl_handle);
    return !out_cached_instances.empty();
}

bool SourceModelManager::LoadModel(char const* model_name, int skin, matrix3x4_t const& model_to_world,
    bool use_per_vertex_lighting, RenderScene& io_scene,
    gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer,
    std::unordered_map<gpu::Image*, gpu::ImageLayout>& image_layouts, SourceTextureManager& texture_manager,
    SourceMaterialManager& material_manager)
{
    std::string model_key = BuildModelCacheKey(model_name, skin);
    auto existing_instance = model_instances_by_key_.find(model_key);
    if (existing_instance != model_instances_by_key_.end())
    {
        return AppendInstanceRanges(existing_instance->second, model_to_world, use_per_vertex_lighting, io_scene);
    }

    std::vector<RenderInstance> cached_instances;
    if (!AppendLoadedModelGeometry(model_name, skin, io_scene, device, cmd_buffer, image_layouts,
            texture_manager, material_manager, cached_instances))
    {
        return false;
    }

    model_instances_by_key_.emplace(model_key, cached_instances);

    return AppendInstanceRanges(cached_instances, model_to_world, use_per_vertex_lighting, io_scene);
}
