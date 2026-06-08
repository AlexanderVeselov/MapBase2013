#include "cbase.h"
#include "draw_scene_task.h"

namespace
{
constexpr uint32_t kMaxMaterialTextures = 512;
}

void DrawSceneTask::Initialize(gpu::DevicePtr const& device, gpu::BufferPtr const& view_proj_buffer,
    RenderScene const& scene)
{
    (void)view_proj_buffer;
    (void)scene;

    gpu::GraphicsPipelineDesc pipeline_desc;
    pipeline_desc.vs_filename = "render_scene.vs";
    pipeline_desc.ps_filename = "render_scene.ps";
    pipeline_desc.color_attachment_formats = {gpu::ImageFormat::kBGRA8_UNorm};
    pipeline_desc.depth_enabled = true;
    pipeline_desc.depth_attachment_format = gpu::ImageFormat::kR32_Typeless;
    pipeline_ = device->CreateGraphicsPipeline(pipeline_desc);

    gpu::SamplerDesc sampler_desc;
    sampler_desc.min_filter = gpu::SamplerFilter::kLinear;
    sampler_desc.mag_filter = gpu::SamplerFilter::kLinear;
    sampler_desc.address_u = gpu::SamplerAddressMode::kRepeat;
    sampler_desc.address_v = gpu::SamplerAddressMode::kRepeat;
    texture_sampler_ = device->GetSampler(sampler_desc);

    gpu::SamplerDesc lightmap_sampler_desc;
    lightmap_sampler_desc.min_filter = gpu::SamplerFilter::kLinear;
    lightmap_sampler_desc.mag_filter = gpu::SamplerFilter::kLinear;
    lightmap_sampler_desc.address_u = gpu::SamplerAddressMode::kClampToEdge;
    lightmap_sampler_desc.address_v = gpu::SamplerAddressMode::kClampToEdge;
    lightmap_sampler_ = device->GetSampler(lightmap_sampler_desc);

    descriptor_set_ = pipeline_->CreateDescriptorSet();
}

void DrawSceneTask::UpdateSceneBindings(gpu::BufferPtr const& view_proj_buffer, RenderScene const& scene,
    SourceTextureManager const& texture_manager)
{
    std::vector<gpu::ImageDescriptor> image_descriptors(kMaxMaterialTextures);
    texture_manager.BuildDescriptorArray(kMaxMaterialTextures, image_descriptors);

    descriptor_set_->Clear();
    descriptor_set_->BindBuffer(*view_proj_buffer, 0);
    descriptor_set_->BindBuffer(*scene.transforms.GpuBuffer(), 1);
    descriptor_set_->BindBuffer(*scene.instances.GpuBuffer(), 2);
    descriptor_set_->BindBuffer(*scene.vertex_colors.GpuBuffer(), 3);
    descriptor_set_->BindBuffer(*scene.materials.GpuBuffer(), 4);
    descriptor_set_->BindImageArray(image_descriptors, 0, 1);
    descriptor_set_->BindSampler(*texture_sampler_, 0, 2);
    descriptor_set_->BindSampler(*lightmap_sampler_, 1, 2);
    descriptor_set_->BindImage(*(scene.lightmap_texture ? scene.lightmap_texture : scene.fallback_lightmap_texture), 0, 3);
}

char const* DrawSceneTask::GetName() const
{
    return "DrawScene";
}

void DrawSceneTask::Execute(RenderTaskContext& context)
{
    TransitionRenderImage(context.backend, context.backend_resources.color_texture, gpu::ImageLayout::kRenderTarget);
    TransitionRenderImage(context.backend, context.backend_resources.depth_texture, gpu::ImageLayout::kRenderTarget);
    context.backend.cmd_buffer->SetRenderTarget(context.backend_resources.color_texture, context.backend_resources.depth_texture);
    context.backend.cmd_buffer->ClearDepthImage(context.backend_resources.depth_texture, 1.0f);
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

        context.backend.cmd_buffer->SetRootConstants(&instance_index, sizeof(instance_index));
        context.backend.cmd_buffer->DrawIndexed(instance.index_count, 1, instance.index_offset,
            static_cast<int32_t>(instance.vertex_offset), instance_index);
    }
}
