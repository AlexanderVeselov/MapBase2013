#include "render_new.h"

#ifdef SOURCE_SDK_RENDER_NEW

#include "dx9_interop.h"
#include "mathlib/vmatrix.h"
#include "bsp_loader.h"
#include "bone_setup.h"
#include "cdll_client_int.h"

#include "bitmap/imageformat.h"
#include "datacache/imdlcache.h"
#include "engine/ivmodelinfo.h"
#include "filesystem.h"
#include "istudiorender.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/itexture.h"
#include "tier1/KeyValues.h"
#include "tier1/utlbuffer.h"
#include "texture_group_names.h"
#include "vtf/vtf.h"

#include "gpu_api.hpp"
#include "gpu_buffer.hpp"
#include "gpu_command_buffer.hpp"
#include "gpu_descriptor_set.hpp"
#include "gpu_device.hpp"
#include "gpu_image.hpp"
#include "gpu_pipeline.hpp"
#include "gpu_queue.hpp"
#include "gpu_sampler.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

extern IMaterialSystem* materials;

class RenderImpl : public RenderNew
{
public:
    void Init() override;
    void LoadLevel(char const* level_name) override;
    void RenderView(ViewSetup const& view_setup) override;

private:
    void EnsureCommandBuffer();
    void TransitionImage(gpu::ImagePtr const& image, gpu::ImageLayout desired_layout);
    void SubmitAndWait();
    gpu::ImagePtr CreateTextureImage(uint32_t width, uint32_t height, void const* data, size_t data_size);
    void RebuildMaterialBindings(std::vector<BspMaterial> const& bsp_materials, std::vector<Vertex>& vertices);

private:
    std::unique_ptr<gpu::Api> api_;
    gpu::DevicePtr device_;
    gpu::Queue* graphics_queue_ = nullptr;
    gpu::CommandBufferPtr cmd_buffer_;
    std::unordered_map<gpu::Image*, gpu::ImageLayout> image_layouts_;
    gpu::ImagePtr color_texture_;
    gpu::ImagePtr depth_texture_;
    gpu::ImagePtr shared_depth_texture_;
    gpu::GraphicsPipelinePtr pipeline_;
    gpu::ComputePipelinePtr copy_depth_pipeline_;
    gpu::DescriptorSetPtr pipeline_descriptor_set_;
    gpu::DescriptorSetPtr copy_depth_descriptor_set_;
    gpu::BufferPtr vertex_buffer_;
    gpu::BufferPtr scene_transform_buffer_;
    gpu::BufferPtr view_proj_buffer_;
    gpu::SamplerPtr texture_sampler_;
    gpu::SamplerPtr lightmap_sampler_;
    gpu::ImagePtr fallback_texture_;
    gpu::ImagePtr fallback_lightmap_texture_;
    gpu::ImagePtr lightmap_texture_;
    std::vector<gpu::ImagePtr> material_textures_;
    uint32_t vertex_count_ = 0;
    std::string shader_dir_;
};

namespace
{
constexpr uint32_t kMaxMaterialTextures = 512;

struct SceneTransform
{
    float m[4][4] = {};
};

std::string GetShaderDirectory()
{
    std::string file_path = __FILE__;
    size_t last_separator = file_path.find_last_of("\\/");
    if (last_separator == std::string::npos)
    {
        return "shaders";
    }

    return file_path.substr(0, last_separator) + "\\shaders";
}

std::string StripExtension(std::string path)
{
    size_t extension_pos = path.find_last_of('.');
    if (extension_pos != std::string::npos)
    {
        path.erase(extension_pos);
    }
    return path;
}

KeyValues* GetMaterialRoot(KeyValues& material_kv)
{
    KeyValues* root = material_kv.GetFirstTrueSubKey();
    return root ? root : &material_kv;
}

std::array<uint8_t, 16> MakeFallbackTexturePixels()
{
    return {255, 0, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 0, 255, 255};
}

SceneTransform MakeIdentitySceneTransform()
{
    SceneTransform transform = {};
    transform.m[0][0] = 1.0f;
    transform.m[1][1] = 1.0f;
    transform.m[2][2] = 1.0f;
    transform.m[3][3] = 1.0f;
    return transform;
}

SceneTransform MakeSceneTransform(matrix3x4_t const& source_transform)
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

IMaterial* FindNamedMaterial(char const* material_name)
{
    if (!material_name || material_name[0] == '\0')
    {
        return nullptr;
    }

    IMaterial* material = materials->FindMaterial(material_name, TEXTURE_GROUP_WORLD, false);
    if (material && !IsErrorMaterial(material))
    {
        return material;
    }

    material = materials->FindMaterial(material_name, TEXTURE_GROUP_MODEL, false);
    if (material && !IsErrorMaterial(material))
    {
        return material;
    }

    return nullptr;
}

float GetVector4DComponent(Vector4D const& value, int index)
{
    switch (index)
    {
    case 0: return value.x;
    case 1: return value.y;
    case 2: return value.z;
    case 3: return value.w;
    default: return 0.0f;
    }
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

#if 0
    Vector position(0.0f, 0.0f, 0.0f);
    Vector normal(0.0f, 0.0f, 0.0f);
    float total_weight = 0.0f;

    for (int bone_weight_index = 0; bone_weight_index < source_vertex.m_NumBones && bone_weight_index < 4; ++bone_weight_index)
    {
        float weight = GetVector4DComponent(source_vertex.m_BoneWeight, bone_weight_index);
        int bone_index = source_vertex.m_BoneIndex[bone_weight_index];
        if (weight <= 0.0f || bone_index < 0 || bone_index >= MAXSTUDIOBONES)
        {
            continue;
        }

        Vector transformed_position;
        Vector transformed_normal;
        VectorTransform(source_vertex.m_Position, pose_to_world[bone_index], transformed_position);
        VectorRotate(source_vertex.m_Normal, pose_to_world[bone_index], transformed_normal);

        position += transformed_position * weight;
        normal += transformed_normal * weight;
        total_weight += weight;
    }

    if (total_weight <= 0.0f)
    {
        position = source_vertex.m_Position;
        normal = source_vertex.m_Normal;
    }
    else if (total_weight != 1.0f)
    {
        float weight_scale = 1.0f / total_weight;
        position *= weight_scale;
        normal *= weight_scale;
    }

    if (normal.Dot(normal) > 0.0f)
    {
        normal.NormalizeInPlace();
    }

    return Vertex{position, normal, {source_vertex.m_TexCoord.x, source_vertex.m_TexCoord.y},
        {fallback_lightmap_u, fallback_lightmap_v}, texture_index};
#endif
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
                    triangle_output.m_PoseToWorld,
                    texture_index, fallback_lightmap_u, fallback_lightmap_v));
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

    Msg("render_new: static props instances=%d rendered=%d triangles=%d\n",
        static_props.size(), appended_prop_count, appended_triangle_count);
}
}

void ComputeViewMatrix(VMatrix* pViewMatrix, const Vector& origin, const QAngle& angles)
{
    static VMatrix baseRotation;
    static bool bDidInit;

    if (!bDidInit)
    {
        MatrixBuildRotationAboutAxis(baseRotation, Vector(1, 0, 0), -90);
        MatrixRotate(baseRotation, Vector(0, 0, 1), 90);
        bDidInit = true;
    }

    *pViewMatrix = baseRotation;
    MatrixRotate(*pViewMatrix, Vector(1, 0, 0), -angles[2]);
    MatrixRotate(*pViewMatrix, Vector(0, 1, 0), -angles[0]);
    MatrixRotate(*pViewMatrix, Vector(0, 0, 1), -angles[1]);

    MatrixTranslate(*pViewMatrix, -origin);
}

// Taken from gl_rmain.cpp: 543
void ComputeViewMatrices(ViewSetup const& view_setup, VMatrix* pWorldToView, VMatrix* pViewToProjection, VMatrix* pWorldToProjection)
{
    ComputeViewMatrix(pWorldToView, view_setup.origin, view_setup.angles);
    MatrixBuildPerspectiveX(*pViewToProjection, view_setup.fov, view_setup.m_flAspectRatio,
        view_setup.zNear, view_setup.zFar);
    MatrixMultiply(*pViewToProjection, *pWorldToView, *pWorldToProjection);
}

void RenderImpl::Init()
{
    DX9_InitD3D9Interop();
    api_.reset(gpu::Api::Create(gpu::ApiType::kD3D12));

    shader_dir_ = GetShaderDirectory();
    api_->SetShaderPath(shader_dir_.c_str());

    device_ = CreateD3D12DeviceForD3D9Adapter(*api_);
    graphics_queue_ = &device_->GetQueue(gpu::QueueType::kGraphics);

    InitSharedTextures(*device_, color_texture_, shared_depth_texture_);
    depth_texture_ = device_->CreateImage(color_texture_->GetWidth(), color_texture_->GetHeight(),
        gpu::ImageFormat::kR32_Typeless, gpu::ImageFlags::kShaderResource | gpu::ImageFlags::kDepthStencil);

    gpu::GraphicsPipelineDesc pipeline_desc;
    pipeline_desc.vs_filename = "render_new.vs";
    pipeline_desc.ps_filename = "render_new.ps";
    pipeline_desc.color_attachment_formats = {gpu::ImageFormat::kBGRA8_UNorm};
    pipeline_desc.depth_enabled = true;
    pipeline_desc.depth_attachment_format = gpu::ImageFormat::kR32_Typeless;
    pipeline_ = device_->CreateGraphicsPipeline(pipeline_desc);
    copy_depth_pipeline_ = device_->CreateComputePipeline("copy_depth.cs");

    view_proj_buffer_ = device_->CreateBuffer(sizeof(VMatrix), sizeof(VMatrix),
        gpu::BufferFlags::kCpuAccess | gpu::BufferFlags::kConstant);

    gpu::SamplerDesc sampler_desc;
    sampler_desc.min_filter = gpu::SamplerFilter::kLinear;
    sampler_desc.mag_filter = gpu::SamplerFilter::kLinear;
    sampler_desc.address_u = gpu::SamplerAddressMode::kRepeat;
    sampler_desc.address_v = gpu::SamplerAddressMode::kRepeat;
    texture_sampler_ = device_->GetSampler(sampler_desc);

    gpu::SamplerDesc lightmap_sampler_desc;
    lightmap_sampler_desc.min_filter = gpu::SamplerFilter::kLinear;
    lightmap_sampler_desc.mag_filter = gpu::SamplerFilter::kLinear;
    lightmap_sampler_desc.address_u = gpu::SamplerAddressMode::kClampToEdge;
    lightmap_sampler_desc.address_v = gpu::SamplerAddressMode::kClampToEdge;
    lightmap_sampler_ = device_->GetSampler(lightmap_sampler_desc);

    std::array<uint8_t, 16> fallback_pixels = MakeFallbackTexturePixels();
    fallback_texture_ = CreateTextureImage(2, 2, fallback_pixels.data(), fallback_pixels.size());
    std::array<uint8_t, 4> fallback_lightmap_pixels = {255, 255, 255, 255};
    fallback_lightmap_texture_ = CreateTextureImage(1, 1, fallback_lightmap_pixels.data(), fallback_lightmap_pixels.size());
    lightmap_texture_ = fallback_lightmap_texture_;

    SceneTransform identity_transform = MakeIdentitySceneTransform();
    scene_transform_buffer_ = device_->CreateBuffer(sizeof(SceneTransform), sizeof(SceneTransform),
        gpu::BufferFlags::kCpuAccess | gpu::BufferFlags::kShaderResource);
    void* transform_data = scene_transform_buffer_->Map();
    std::memcpy(transform_data, &identity_transform, sizeof(identity_transform));
    scene_transform_buffer_->Unmap();

    pipeline_descriptor_set_ = pipeline_->CreateDescriptorSet();
    pipeline_descriptor_set_->BindBuffer(*view_proj_buffer_, 0);
    pipeline_descriptor_set_->BindBuffer(*scene_transform_buffer_, 1);
    pipeline_descriptor_set_->BindSampler(*texture_sampler_, 0, 2);
    pipeline_descriptor_set_->BindSampler(*lightmap_sampler_, 1, 2);
    pipeline_descriptor_set_->BindImage(*lightmap_texture_, 0, 3);

    copy_depth_descriptor_set_ = copy_depth_pipeline_->CreateDescriptorSet();
    copy_depth_descriptor_set_->BindImage(*depth_texture_, 0);
    copy_depth_descriptor_set_->BindImage(*shared_depth_texture_, 1);
}

void RenderImpl::LoadLevel(char const* level_name)
{
    std::vector<Vertex> cpu_vertices;
    std::vector<BspMaterial> bsp_materials;
    std::vector<SceneTransform> scene_transforms;
    scene_transforms.push_back(MakeIdentitySceneTransform());
    BspLightmapAtlas lightmap_atlas;
    LoadBsp(level_name, cpu_vertices, bsp_materials, lightmap_atlas);
    AppendStaticPropTriangles(level_name, cpu_vertices, bsp_materials, lightmap_atlas, scene_transforms);

    if (!lightmap_atlas.rgba_pixels.empty() && lightmap_atlas.width > 0 && lightmap_atlas.height > 0)
    {
        lightmap_texture_ = CreateTextureImage(static_cast<uint32_t>(lightmap_atlas.width),
            static_cast<uint32_t>(lightmap_atlas.height), lightmap_atlas.rgba_pixels.data(), lightmap_atlas.rgba_pixels.size());
    }
    else
    {
        lightmap_texture_ = fallback_lightmap_texture_;
    }

    scene_transform_buffer_ = device_->CreateBuffer(sizeof(SceneTransform) * scene_transforms.size(), sizeof(SceneTransform),
        gpu::BufferFlags::kCpuAccess | gpu::BufferFlags::kShaderResource);
    void* transform_data = scene_transform_buffer_->Map();
    std::memcpy(transform_data, scene_transforms.data(), sizeof(SceneTransform) * scene_transforms.size());
    scene_transform_buffer_->Unmap();

    RebuildMaterialBindings(bsp_materials, cpu_vertices);

    vertex_count_ = static_cast<uint32_t>(cpu_vertices.size());
    vertex_buffer_ = device_->CreateBuffer(sizeof(Vertex) * cpu_vertices.size(), sizeof(Vertex),
        gpu::BufferFlags::kCpuAccess);

    void* mapped_data = vertex_buffer_->Map();
    std::memcpy(mapped_data, cpu_vertices.data(), sizeof(Vertex) * cpu_vertices.size());
    vertex_buffer_->Unmap();
}

void RenderImpl::RenderView(ViewSetup const& view_setup)
{
    uint32_t viewport_width = color_texture_->GetWidth();
    uint32_t viewport_height = color_texture_->GetHeight();
    EnsureCommandBuffer();
    cmd_buffer_->SetViewport(gpu::Viewport{0.0f, 0.0f, static_cast<float>(viewport_width),
        static_cast<float>(viewport_height), 0.0f, 1.0f});
    cmd_buffer_->SetScissor(gpu::Rect{0, 0, static_cast<int32_t>(viewport_width), static_cast<int32_t>(viewport_height)});

    VMatrix view_matrix, projection_matrix, view_projection_matrix;
    ComputeViewMatrices(view_setup, &view_matrix, &projection_matrix, &view_projection_matrix);
    MatrixTranspose(view_projection_matrix, view_projection_matrix);

    void* mapped_data = view_proj_buffer_->Map();
    std::memcpy(mapped_data, view_projection_matrix.Base(), sizeof(VMatrix));
    view_proj_buffer_->Unmap();

    TransitionImage(color_texture_, gpu::ImageLayout::kRenderTarget);
    TransitionImage(depth_texture_, gpu::ImageLayout::kRenderTarget);
    cmd_buffer_->SetRenderTarget(color_texture_, depth_texture_);

    cmd_buffer_->ClearImage(color_texture_, 0.0f, 0.5f, 0.5f, 1.0f);
    cmd_buffer_->ClearDepthImage(depth_texture_, 1.0f);
    cmd_buffer_->BindPipeline(pipeline_);
    cmd_buffer_->BindDescriptorSet(pipeline_descriptor_set_);
    cmd_buffer_->SetVertexBuffer(vertex_buffer_, sizeof(Vertex));
    cmd_buffer_->Draw(vertex_count_);

    TransitionImage(depth_texture_, gpu::ImageLayout::kShaderRead);
    TransitionImage(shared_depth_texture_, gpu::ImageLayout::kShaderReadWrite);
    cmd_buffer_->BindPipeline(copy_depth_pipeline_);
    cmd_buffer_->BindDescriptorSet(copy_depth_descriptor_set_);
    cmd_buffer_->Dispatch((viewport_width + 15) / 16, (viewport_height + 15) / 16, 1);
    cmd_buffer_->StorageBarrier(shared_depth_texture_);

    SubmitAndWait();

    DX9_RenderFrame();
}

void RenderImpl::EnsureCommandBuffer()
{
    if (!cmd_buffer_)
    {
        cmd_buffer_ = graphics_queue_->CreateCommandBuffer();
    }
}

void RenderImpl::TransitionImage(gpu::ImagePtr const& image, gpu::ImageLayout desired_layout)
{
    gpu::ImageLayout& current_layout = image_layouts_[image.get()];
    if (current_layout == desired_layout)
    {
        return;
    }

    EnsureCommandBuffer();
    cmd_buffer_->TransitionBarrier(image, current_layout, desired_layout);
    current_layout = desired_layout;
}

void RenderImpl::SubmitAndWait()
{
    if (!cmd_buffer_)
    {
        return;
    }

    graphics_queue_->Submit(std::move(cmd_buffer_));
    graphics_queue_->WaitIdle();
}

gpu::ImagePtr RenderImpl::CreateTextureImage(uint32_t width, uint32_t height, void const* data, size_t data_size)
{
    gpu::ImagePtr image =
        device_->CreateImage(width, height, gpu::ImageFormat::kRGBA8_UNorm, gpu::ImageFlags::kShaderResource);
    TransitionImage(image, gpu::ImageLayout::kCopyDst);
    cmd_buffer_->UploadImage(image, data, data_size);
    TransitionImage(image, gpu::ImageLayout::kShaderRead);
    return image;
}

bool ResolveBaseTextureName(char const* material_name, std::string& out_texture_name)
{
    if (!material_name || material_name[0] == '\0')
    {
        return false;
    }

    IMaterial* material = FindNamedMaterial(material_name);
    if (!material)
    {
        return false;
    }

    bool found = false;
    IMaterialVar* base_texture_var = material->FindVar("$basetexture", &found, false);
    if (!found || !base_texture_var)
    {
        base_texture_var = material->FindVar("%tooltexture", &found, false);
        if (!found || !base_texture_var)
        {
            return false;
        }
    }

    ITexture* texture = base_texture_var->GetTextureValue();
    if (texture && !texture->IsError())
    {
        out_texture_name = StripExtension(texture->GetName());
        std::replace(out_texture_name.begin(), out_texture_name.end(), '\\', '/');
        return !out_texture_name.empty();
    }

    char const* base_texture_name = base_texture_var->GetStringValue();
    if (!base_texture_name[0])
    {
        return false;
    }

    out_texture_name = StripExtension(base_texture_name);
    std::replace(out_texture_name.begin(), out_texture_name.end(), '\\', '/');
    return !out_texture_name.empty();
}

bool LoadTextureRgba(char const* texture_name, std::vector<uint8_t>& out_pixels, uint32_t& out_width, uint32_t& out_height)
{
    if (!texture_name || texture_name[0] == '\0')
    {
        return false;
    }

    std::string vtf_path = "materials/" + StripExtension(texture_name) + ".vtf";
    CUtlBuffer buffer;
    if (!g_pFullFileSystem->ReadFile(vtf_path.c_str(), "GAME", buffer))
    {
        return false;
    }

    IVTFTexture* vtf_texture = CreateVTFTexture();
    if (!vtf_texture)
    {
        return false;
    }

    bool success = false;
    do
    {
        if (!vtf_texture->Unserialize(buffer))
        {
            break;
        }

        int width = vtf_texture->Width();
        int height = vtf_texture->Height();
        if (width <= 0 || height <= 0)
        {
            break;
        }

        constexpr ::ImageFormat kDstFormat = IMAGE_FORMAT_RGBA8888;
        size_t image_size = static_cast<size_t>(ImageLoader::GetMemRequired(width, height, 1, kDstFormat, false));
        out_pixels.resize(image_size);

        if (!ImageLoader::ConvertImageFormat(vtf_texture->ImageData(0, 0, 0), vtf_texture->Format(), out_pixels.data(),
                kDstFormat, width, height, 0, 0))
        {
            out_pixels.clear();
            break;
        }

        out_width = static_cast<uint32_t>(width);
        out_height = static_cast<uint32_t>(height);
        success = true;
    } while (false);

    DestroyVTFTexture(vtf_texture);
    return success;
}

void RenderImpl::RebuildMaterialBindings(std::vector<BspMaterial> const& bsp_materials, std::vector<Vertex>& vertices)
{
    material_textures_.clear();
    material_textures_.resize(bsp_materials.size() + 1);
    material_textures_[0] = fallback_texture_;

    EnsureCommandBuffer();

    for (size_t material_index = 0; material_index < bsp_materials.size(); ++material_index)
    {
        if (material_index + 1 >= kMaxMaterialTextures)
        {
            break;
        }

        std::string base_texture_name;
        std::vector<uint8_t> rgba_pixels;
        uint32_t width = 0;
        uint32_t height = 0;

        gpu::ImagePtr texture_image = fallback_texture_;
        if (ResolveBaseTextureName(bsp_materials[material_index].material_name.c_str(), base_texture_name)
            && LoadTextureRgba(base_texture_name.c_str(), rgba_pixels, width, height))
        {
            texture_image = CreateTextureImage(width, height, rgba_pixels.data(), rgba_pixels.size());
        }

        material_textures_[material_index + 1] = texture_image;
    }

    for (Vertex& vertex : vertices)
    {
        if (vertex.texture_index >= kMaxMaterialTextures || vertex.texture_index >= material_textures_.size())
        {
            vertex.texture_index = 0;
        }
    }

    std::vector<gpu::ImageDescriptor> image_descriptors(kMaxMaterialTextures);
    for (uint32_t texture_index = 0; texture_index < kMaxMaterialTextures; ++texture_index)
    {
        gpu::ImagePtr const& image = texture_index < material_textures_.size() && material_textures_[texture_index]
            ? material_textures_[texture_index]
            : fallback_texture_;
        image_descriptors[texture_index] = gpu::ImageDescriptor{image.get(), {}};
    }

    pipeline_descriptor_set_->Clear();
    pipeline_descriptor_set_->BindBuffer(*view_proj_buffer_, 0);
    pipeline_descriptor_set_->BindBuffer(*scene_transform_buffer_, 1);
    pipeline_descriptor_set_->BindImageArray(image_descriptors, 0, 1);
    pipeline_descriptor_set_->BindSampler(*texture_sampler_, 0, 2);
    pipeline_descriptor_set_->BindSampler(*lightmap_sampler_, 1, 2);
    pipeline_descriptor_set_->BindImage(*(lightmap_texture_ ? lightmap_texture_ : fallback_lightmap_texture_), 0, 3);

    SubmitAndWait();
}

RenderNew* GetRenderNewInstance()
{
    static RenderImpl instance;
    return &instance;
}

#else

class RenderNull : public RenderNew
{
public:
    void Init() override {}
    void LoadLevel(char const* level_name) override {}
    void RenderView(ViewSetup const& view_setup) override {}
};

RenderNew* GetRenderNewInstance()
{
    static RenderNull instance;
    return &instance;
}

#endif
