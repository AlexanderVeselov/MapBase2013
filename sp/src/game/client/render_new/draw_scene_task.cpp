#include "cbase.h"
#include "draw_scene_task.h"

namespace
{
constexpr uint32_t kMaxMaterialTextures = 512;
}

void DrawSceneTask::Initialize(gpu::DevicePtr const& device, gpu::BufferPtr const& view_proj_buffer,
    gpu::SamplerPtr const& texture_sampler, gpu::SamplerPtr const& lightmap_sampler, RenderSceneGpu const& gpu_scene)
{
    (void)view_proj_buffer;
    (void)texture_sampler;
    (void)lightmap_sampler;
    (void)gpu_scene;

    gpu::GraphicsPipelineDesc pipeline_desc;
    pipeline_desc.vs_filename = "render_new.vs";
    pipeline_desc.ps_filename = "render_new.ps";
    pipeline_desc.color_attachment_formats = {gpu::ImageFormat::kBGRA8_UNorm};
    pipeline_desc.depth_enabled = true;
    pipeline_desc.depth_attachment_format = gpu::ImageFormat::kR32_Typeless;
    pipeline_ = device->CreateGraphicsPipeline(pipeline_desc);

    descriptor_set_ = pipeline_->CreateDescriptorSet();
}

void DrawSceneTask::UpdateSceneBindings(gpu::BufferPtr const& view_proj_buffer, gpu::SamplerPtr const& texture_sampler,
    gpu::SamplerPtr const& lightmap_sampler, RenderSceneGpu const& gpu_scene)
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
    descriptor_set_->BindImageArray(image_descriptors, 0, 1);
    descriptor_set_->BindSampler(*texture_sampler, 0, 2);
    descriptor_set_->BindSampler(*lightmap_sampler, 1, 2);
    descriptor_set_->BindImage(*(gpu_scene.lightmap_texture ? gpu_scene.lightmap_texture : gpu_scene.fallback_lightmap_texture), 0, 3);
}

char const* DrawSceneTask::GetName() const
{
    return "DrawScene";
}

void DrawSceneTask::Execute(RenderTaskContext& context)
{
    context.backend.cmd_buffer->ClearImage(context.backend_resources.color_texture, 0.0f, 0.5f, 0.5f, 1.0f);
    context.backend.cmd_buffer->ClearDepthImage(context.backend_resources.depth_texture, 1.0f);
    context.backend.cmd_buffer->BindPipeline(pipeline_);
    context.backend.cmd_buffer->BindDescriptorSet(descriptor_set_);
    context.backend.cmd_buffer->SetVertexBuffer(context.gpu_scene.vertex_buffer, sizeof(Vertex));
    context.backend.cmd_buffer->Draw(context.gpu_scene.vertex_count);
}
