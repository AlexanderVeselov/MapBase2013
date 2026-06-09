#include "cbase.h"
#include "render_shadowmaps_task.h"

#include "../render_backend.h"
#include "mathlib/vmatrix.h"

#include <cfloat>
#include <cmath>
#include <cstring>

namespace
{
constexpr uint32_t kMaxMaterialTextures = 512;
constexpr float kCasterDepthPadding = 512.0f;
constexpr float kXYRadiusPadding = 32.0f;

VMatrix BuildLookAtMatrix(Vector const& eye, Vector const& target, Vector const& up)
{
    Vector backward = eye - target;
    VectorNormalize(backward);

    Vector right;
    CrossProduct(up, backward, right);
    VectorNormalize(right);

    Vector adjusted_up;
    CrossProduct(backward, right, adjusted_up);
    VectorNormalize(adjusted_up);

    VMatrix world_to_view;
    world_to_view.Init(
        right.x, right.y, right.z, -DotProduct(right, eye),
        adjusted_up.x, adjusted_up.y, adjusted_up.z, -DotProduct(adjusted_up, eye),
        backward.x, backward.y, backward.z, -DotProduct(backward, eye),
        0.0f, 0.0f, 0.0f, 1.0f);
    return world_to_view;
}
}

void RenderShadowmapsTask::Initialize(gpu::DevicePtr const& device, RenderScene const& scene,
    SourceTextureManager const& texture_manager)
{
    gpu::GraphicsPipelineDesc pipeline_desc;
    pipeline_desc.vs_filename = "render_shadowmap.vs";
    pipeline_desc.ps_filename = "render_shadowmap.ps";
    pipeline_desc.cull_mode = gpu::CullMode::kNone;
    pipeline_desc.depth_enabled = true;
    pipeline_desc.depth_write_enabled = true;
    pipeline_desc.depth_attachment_format = gpu::ImageFormat::kR32_Typeless;
    pipeline_ = device->CreateGraphicsPipeline(pipeline_desc);

    shadow_camera_buffer_ = device->CreateBuffer(sizeof(VMatrix), sizeof(VMatrix), gpu::BufferFlags::kConstant);

    gpu::SamplerDesc sampler_desc;
    sampler_desc.min_filter = gpu::SamplerFilter::kLinear;
    sampler_desc.mag_filter = gpu::SamplerFilter::kLinear;
    sampler_desc.address_u = gpu::SamplerAddressMode::kRepeat;
    sampler_desc.address_v = gpu::SamplerAddressMode::kRepeat;
    sampler_desc.max_anisotropy = 8;
    texture_sampler_ = device->GetSampler(sampler_desc);

    descriptor_set_ = pipeline_->CreateDescriptorSet();
    UpdateSceneBindings(scene, texture_manager);
}

void RenderShadowmapsTask::UpdateSceneBindings(RenderScene const& scene, SourceTextureManager const& texture_manager)
{
    std::vector<gpu::ImageDescriptor> image_descriptors(kMaxMaterialTextures);
    texture_manager.BuildDescriptorArray(kMaxMaterialTextures, image_descriptors);

    descriptor_set_->Clear();
    descriptor_set_->BindBuffer(*shadow_camera_buffer_, 0);
    descriptor_set_->BindBuffer(*scene.transforms.GpuBuffer(), 1);
    descriptor_set_->BindBuffer(*scene.instances.GpuBuffer(), 2);
    descriptor_set_->BindBuffer(*scene.bones.GpuBuffer(), 4);
    descriptor_set_->BindBuffer(*scene.materials.GpuBuffer(), 8);
    descriptor_set_->BindImageArray(image_descriptors, 0, 1);
    descriptor_set_->BindSampler(*texture_sampler_, 0, 2);
}

char const* RenderShadowmapsTask::GetName() const
{
    return "RenderShadowmaps";
}

bool RenderShadowmapsTask::BuildDirectionalShadowMatrix(
    std::array<Vector, 8> const& frustum_corners,
    SceneLight const& light,
    VMatrix& out_world_to_shadow) const
{
    Vector light_dir(light.direction[0], light.direction[1], light.direction[2]);
    if (light_dir.LengthSqr() < 1e-6f)
    {
        return false;
    }

    VectorNormalize(light_dir);

    Vector up(0.0f, 0.0f, 1.0f);
    if (fabs(DotProduct(light_dir, up)) > 0.95f)
    {
        up = Vector(0.0f, 1.0f, 0.0f);
    }

    Vector center(0.0f, 0.0f, 0.0f);
    for (Vector const& corner : frustum_corners)
    {
        center += corner;
    }
    center /= 8.0f;

    constexpr float kLightDistance = 10000.0f;
    Vector eye = center - light_dir * kLightDistance;

    VMatrix world_to_light = BuildLookAtMatrix(eye, center, up);

    Vector light_min(FLT_MAX, FLT_MAX, FLT_MAX);
    Vector light_max(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    float light_min_z = FLT_MAX;
    float light_max_z = -FLT_MAX;

    for (Vector const& corner : frustum_corners)
    {
        Vector light_space_corner;
        Vector3DMultiplyPosition(world_to_light, corner, light_space_corner);

        light_min.x = Min(light_min.x, light_space_corner.x);
        light_min.y = Min(light_min.y, light_space_corner.y);
        light_min_z = Min(light_min_z, light_space_corner.z);
        light_max.x = Max(light_max.x, light_space_corner.x);
        light_max.y = Max(light_max.y, light_space_corner.y);
        light_max_z = Max(light_max_z, light_space_corner.z);
    }

    light_min.x -= kXYRadiusPadding;
    light_min.y -= kXYRadiusPadding;
    light_max.x += kXYRadiusPadding;
    light_max.y += kXYRadiusPadding;
    light_min_z -= kCasterDepthPadding;
    light_max_z += kCasterDepthPadding;

    light_min.x = -1024.0f;
    light_max.x = 1024.0f;
    light_min.y = -1024.0f;
    light_max.y = 1024.0f;
    float light_near_z = Max(1.0f, -light_max_z);
    float light_far_z = Max(light_near_z + 1.0f, -light_min_z);

    VMatrix light_to_clip;
    MatrixBuildOrtho(
        light_to_clip,
        light_min.x,
        light_max.y,
        light_max.x,
        light_min.y,
        light_near_z,
        light_far_z);

    MatrixMultiply(light_to_clip, world_to_light, out_world_to_shadow);
    return true;
}

void RenderShadowmapsTask::Execute(RenderTaskContext& context)
{
    gpu::BufferPtr const& vertex_buffer = context.scene.vertices.GpuBuffer();
    gpu::BufferPtr const& index_buffer = context.scene.indices.GpuBuffer();
    if (!vertex_buffer || !index_buffer || context.scene.instances.Size() == 0 || context.scene.shadowmap_textures.empty())
    {
        return;
    }

    bool updated_shadow_matrices = false;
    context.backend.cmd_buffer->BindPipeline(pipeline_);
    context.backend.cmd_buffer->BindDescriptorSet(descriptor_set_);
    context.backend.cmd_buffer->SetVertexBuffer(vertex_buffer, sizeof(Vertex));
    context.backend.cmd_buffer->SetIndexBuffer(index_buffer);

    for (uint32_t light_index = 0; light_index < context.scene.lights.Size(); ++light_index)
    {
        SceneLight const& light = context.scene.lights[light_index];
        if (light.type != SceneLight::kDirectional || light.shadowmap_index == SceneLight::kInvalidShadowmapIndex)
        {
            continue;
        }

        if (light.shadowmap_index >= context.scene.shadowmap_textures.size())
        {
            continue;
        }

        VMatrix world_to_shadow;
        if (!BuildDirectionalShadowMatrix(context.camera_frustum_corners, light, world_to_shadow))
        {
            continue;
        }

        VMatrix gpu_shadow_matrix;
        MatrixTranspose(world_to_shadow, gpu_shadow_matrix);
        if (light.shadowmap_index < context.scene.shadow_matrices.Size())
        {
            std::memcpy(context.scene.shadow_matrices.Data()[light.shadowmap_index].m, gpu_shadow_matrix.Base(), sizeof(float) * 16);
            updated_shadow_matrices = true;
        }

        UploadBufferData(context.backend.device, *context.backend.cmd_buffer, shadow_camera_staging_buffer_,
            shadow_camera_buffer_, gpu_shadow_matrix.Base(), sizeof(VMatrix));

        gpu::ImagePtr const& shadowmap = context.scene.shadowmap_textures[light.shadowmap_index];
        TransitionRenderImage(context.backend, shadowmap, gpu::ImageLayout::kRenderTarget);
        context.backend.cmd_buffer->SetViewport(gpu::Viewport{0.0f, 0.0f,
            static_cast<float>(RenderScene::kShadowmapResolution), static_cast<float>(RenderScene::kShadowmapResolution),
            0.0f, 1.0f});
        context.backend.cmd_buffer->SetScissor(gpu::Rect{0, 0,
            static_cast<int32_t>(RenderScene::kShadowmapResolution), static_cast<int32_t>(RenderScene::kShadowmapResolution)});
        context.backend.cmd_buffer->SetRenderTargets({}, shadowmap);
        context.backend.cmd_buffer->ClearDepthImage(shadowmap, 1.0f);

        for (uint32_t instance_index = 0; instance_index < context.scene.instances.Size(); ++instance_index)
        {
            RenderInstance const& instance = context.scene.instances[instance_index];
            if (instance.index_count == 0 || instance.is_visible == RenderInstance::kHidden)
            {
                continue;
            }

            if (instance.material_index < context.scene.materials.Size()
                && context.scene.materials[instance.material_index].translucent != 0)
            {
                continue;
            }

            context.backend.cmd_buffer->SetRootConstants(&instance_index, sizeof(instance_index));
            context.backend.cmd_buffer->DrawIndexed(instance.index_count, 1, instance.index_offset,
                static_cast<int32_t>(instance.vertex_offset), instance_index);
        }

        TransitionRenderImage(context.backend, shadowmap, gpu::ImageLayout::kShaderRead);
    }

    if (updated_shadow_matrices)
    {
        context.scene.shadow_matrices.MarkDirtyRange(0, context.scene.shadow_matrices.Size());
        context.scene.shadow_matrices.Sync(context.backend.device, *context.backend.cmd_buffer);
    }

    context.backend.cmd_buffer->SetViewport(gpu::Viewport{0.0f, 0.0f, static_cast<float>(context.viewport_width),
        static_cast<float>(context.viewport_height), 0.0f, 1.0f});
    context.backend.cmd_buffer->SetScissor(gpu::Rect{0, 0, static_cast<int32_t>(context.viewport_width),
        static_cast<int32_t>(context.viewport_height)});
}
