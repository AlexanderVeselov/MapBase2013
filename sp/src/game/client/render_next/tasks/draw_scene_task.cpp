#include "cbase.h"
#include "draw_scene_task.h"

namespace
{
constexpr uint32_t kMaxMaterialTextures = 512;
}

void DrawSceneTask::Initialize(gpu::DevicePtr const& device, gpu::BufferPtr const& view_proj_buffer,
    RenderSceneGpu const& gpu_scene)
{
    (void)view_proj_buffer;
    (void)gpu_scene;

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

void DrawSceneTask::UpdateSceneBindings(gpu::BufferPtr const& view_proj_buffer, RenderSceneGpu const& gpu_scene)
{
    std::vector<gpu::ImageDescriptor> image_descriptors(kMaxMaterialTextures);
    for (uint32_t texture_index = 0; texture_index < kMaxMaterialTextures; ++texture_index)
    {
        gpu::ImagePtr const& image = texture_index < gpu_scene.material_textures.size() && gpu_scene.material_textures[texture_index]
            ? gpu_scene.material_textures[texture_index]
            : gpu_scene.fallback_texture;
        image_descriptors[texture_index] = gpu::ImageDescriptor{image.get(), {}};
    }

    descriptor_set_->Clear();
    descriptor_set_->BindBuffer(*view_proj_buffer, 0);
    descriptor_set_->BindBuffer(*gpu_scene.scene_transform_buffer, 1);
    descriptor_set_->BindBuffer(*gpu_scene.scene_instance_buffer, 2);
    descriptor_set_->BindBuffer(*gpu_scene.scene_vertex_color_buffer, 3);
    descriptor_set_->BindImageArray(image_descriptors, 0, 1);
    descriptor_set_->BindSampler(*texture_sampler_, 0, 2);
    descriptor_set_->BindSampler(*lightmap_sampler_, 1, 2);
    descriptor_set_->BindImage(*(gpu_scene.lightmap_texture ? gpu_scene.lightmap_texture : gpu_scene.fallback_lightmap_texture), 0, 3);
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
    if (!context.gpu_scene.vertex_buffer || context.gpu_scene.instance_count == 0)
    {
        return;
    }

    context.backend.cmd_buffer->BindPipeline(pipeline_);
    context.backend.cmd_buffer->BindDescriptorSet(descriptor_set_);
    context.backend.cmd_buffer->SetVertexBuffer(context.gpu_scene.vertex_buffer, sizeof(Vertex));
    for (uint32_t instance_index = 0; instance_index < context.gpu_scene.uploaded_instances.size(); ++instance_index)
    {
        RenderInstance const& instance = context.gpu_scene.uploaded_instances[instance_index];
        if (instance.vertex_count == 0)
        {
            continue;
        }

        context.backend.cmd_buffer->SetRootConstants(&instance_index, sizeof(instance_index));
        context.backend.cmd_buffer->Draw(instance.vertex_count, 1, instance.first_vertex, instance_index);
    }
}
