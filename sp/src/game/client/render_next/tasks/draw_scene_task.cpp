#include "cbase.h"
#include "draw_scene_task.h"

namespace
{
constexpr uint32_t kMaxMaterialTextures = 512;
}

void DrawSceneTask::Initialize(gpu::DevicePtr const& device, gpu::BufferPtr const& camera_buffer,
    RenderScene const& scene, PassType pass_type)
{
    (void)camera_buffer;
    (void)scene;
    pass_type_ = pass_type;

    gpu::GraphicsPipelineDesc pipeline_desc;
    pipeline_desc.vs_filename = "render_scene.vs";
    pipeline_desc.ps_filename = "render_scene.ps";
    pipeline_desc.cull_mode = pass_type_ == PassType::kOpaque ? gpu::CullMode::kBack : gpu::CullMode::kNone;
    pipeline_desc.color_attachment_formats = {gpu::ImageFormat::kBGRA8_UNorm, gpu::ImageFormat::kRG16_Float};
    pipeline_desc.depth_enabled = true;
    pipeline_desc.depth_write_enabled = pass_type_ == PassType::kOpaque;
    pipeline_desc.depth_attachment_format = gpu::ImageFormat::kR32_Typeless;
    if (pass_type_ == PassType::kTranslucent)
    {
        pipeline_desc.color_blend_attachments.resize(2);
        pipeline_desc.color_blend_attachments[0].blend_enabled = true;
        pipeline_desc.color_blend_attachments[0].src_color_blend_factor = gpu::BlendFactor::kSrcAlpha;
        pipeline_desc.color_blend_attachments[0].dst_color_blend_factor = gpu::BlendFactor::kOneMinusSrcAlpha;
        pipeline_desc.color_blend_attachments[0].src_alpha_blend_factor = gpu::BlendFactor::kOne;
        pipeline_desc.color_blend_attachments[0].dst_alpha_blend_factor = gpu::BlendFactor::kOneMinusSrcAlpha;
        pipeline_desc.color_blend_attachments[1].color_write_mask = gpu::kColorWriteNone;
    }
    pipeline_ = device->CreateGraphicsPipeline(pipeline_desc);

    gpu::SamplerDesc sampler_desc;
    sampler_desc.min_filter = gpu::SamplerFilter::kLinear;
    sampler_desc.mag_filter = gpu::SamplerFilter::kLinear;
    sampler_desc.address_u = gpu::SamplerAddressMode::kRepeat;
    sampler_desc.address_v = gpu::SamplerAddressMode::kRepeat;
    sampler_desc.max_anisotropy = 8;
    texture_sampler_ = device->GetSampler(sampler_desc);

    gpu::SamplerDesc lightmap_sampler_desc;
    lightmap_sampler_desc.min_filter = gpu::SamplerFilter::kLinear;
    lightmap_sampler_desc.mag_filter = gpu::SamplerFilter::kLinear;
    lightmap_sampler_desc.address_u = gpu::SamplerAddressMode::kClampToEdge;
    lightmap_sampler_desc.address_v = gpu::SamplerAddressMode::kClampToEdge;
    lightmap_sampler_ = device->GetSampler(lightmap_sampler_desc);

    descriptor_set_ = pipeline_->CreateDescriptorSet();
}

void DrawSceneTask::UpdateSceneBindings(gpu::BufferPtr const& camera_buffer, RenderScene const& scene,
    SourceTextureManager const& texture_manager)
{
    std::vector<gpu::ImageDescriptor> image_descriptors(kMaxMaterialTextures);
    texture_manager.BuildDescriptorArray(kMaxMaterialTextures, image_descriptors);

    descriptor_set_->Clear();
    descriptor_set_->BindBuffer(*camera_buffer, 0);
    descriptor_set_->BindBuffer(*scene.transforms.GpuBuffer(), 1);
    descriptor_set_->BindBuffer(*scene.instances.GpuBuffer(), 2);
    descriptor_set_->BindBuffer(*scene.vertex_colors.GpuBuffer(), 3);
    descriptor_set_->BindBuffer(*scene.bones.GpuBuffer(), 4);
    descriptor_set_->BindBuffer(*scene.ambient_cubes.GpuBuffer(), 5);
    descriptor_set_->BindBuffer(*(scene.prev_transforms ? scene.prev_transforms : scene.transforms.GpuBuffer()), 6);
    descriptor_set_->BindBuffer(*scene.materials.GpuBuffer(), 7);
    descriptor_set_->BindImageArray(image_descriptors, 0, 1);
    descriptor_set_->BindSampler(*texture_sampler_, 0, 2);
    descriptor_set_->BindSampler(*lightmap_sampler_, 1, 2);
    descriptor_set_->BindImage(*(scene.lightmap_texture ? scene.lightmap_texture : scene.fallback_lightmap_texture), 0, 3);
}

char const* DrawSceneTask::GetName() const
{
    return pass_type_ == PassType::kOpaque ? "DrawSceneOpaque" : "DrawSceneTranslucent";
}

void DrawSceneTask::Execute(RenderTaskContext& context)
{
    if (pass_type_ == PassType::kOpaque)
    {
        TransitionRenderImage(context.backend, context.backend_resources.scene_color_texture, gpu::ImageLayout::kRenderTarget);
        TransitionRenderImage(context.backend, context.backend_resources.velocity_texture, gpu::ImageLayout::kRenderTarget);
        TransitionRenderImage(context.backend, context.backend_resources.depth_texture, gpu::ImageLayout::kRenderTarget);
        context.backend.cmd_buffer->ClearImage(context.backend_resources.velocity_texture, 0.0f, 0.0f, 0.0f, 0.0f);
    }

    context.backend.cmd_buffer->SetRenderTargets(
        {context.backend_resources.scene_color_texture, context.backend_resources.velocity_texture},
        context.backend_resources.depth_texture);
    if (pass_type_ == PassType::kOpaque)
    {
        context.backend.cmd_buffer->ClearDepthImage(context.backend_resources.depth_texture, 1.0f);
    }

    gpu::BufferPtr const& vertex_buffer = context.scene.vertices.GpuBuffer();
    gpu::BufferPtr const& index_buffer = context.scene.indices.GpuBuffer();
    if (!vertex_buffer || !index_buffer || context.scene.instances.Size() == 0)
    {
        return;
    }

    context.backend.cmd_buffer->BindPipeline(pipeline_);
    context.backend.cmd_buffer->BindDescriptorSet(descriptor_set_);
    context.backend.cmd_buffer->SetVertexBuffer(vertex_buffer, sizeof(Vertex));
    context.backend.cmd_buffer->SetIndexBuffer(index_buffer);
    for (uint32_t instance_index = 0; instance_index < context.scene.instances.Size(); ++instance_index)
    {
        RenderInstance const& instance = context.scene.instances[instance_index];
        if (instance.index_count == 0 || instance.is_visible == RenderInstance::kHidden)
        {
            continue;
        }

        bool translucent = false;
        if (instance.material_index < context.scene.materials.Size())
        {
            translucent = context.scene.materials[instance.material_index].translucent != 0;
        }

        if ((pass_type_ == PassType::kTranslucent) != translucent)
        {
            continue;
        }

        context.backend.cmd_buffer->SetRootConstants(&instance_index, sizeof(instance_index));
        context.backend.cmd_buffer->DrawIndexed(instance.index_count, 1, instance.index_offset,
            static_cast<int32_t>(instance.vertex_offset), instance_index);
    }
}
