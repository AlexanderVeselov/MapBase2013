#include "cbase.h"
#include "sky_render_task.h"

namespace
{
constexpr uint32_t kMaxMaterialTextures = 512;
}

void SkyRenderTask::Initialize(gpu::DevicePtr const& device, RenderBackendResources const& backend_resources,
    RenderSceneGpu const& gpu_scene, SourceTextureManager const& texture_manager)
{
    pipeline_ = device->CreateComputePipeline("render_sky.cs");

    gpu::SamplerDesc sampler_desc;
    sampler_desc.min_filter = gpu::SamplerFilter::kLinear;
    sampler_desc.mag_filter = gpu::SamplerFilter::kLinear;
    sampler_desc.address_u = gpu::SamplerAddressMode::kClampToEdge;
    sampler_desc.address_v = gpu::SamplerAddressMode::kClampToEdge;
    sky_sampler_ = device->GetSampler(sampler_desc);

    descriptor_set_ = pipeline_->CreateDescriptorSet();
    UpdateBindings(backend_resources, gpu_scene, texture_manager);
}

void SkyRenderTask::UpdateBindings(RenderBackendResources const& backend_resources, RenderSceneGpu const& gpu_scene,
    SourceTextureManager const& texture_manager)
{
    std::vector<gpu::ImageDescriptor> image_descriptors(kMaxMaterialTextures);
    texture_manager.BuildDescriptorArray(kMaxMaterialTextures, image_descriptors);

    descriptor_set_->Clear();
    descriptor_set_->BindBuffer(*backend_resources.inverse_view_proj_buffer, 0);
    descriptor_set_->BindImageArray(image_descriptors, 0, 1);
    descriptor_set_->BindSampler(*sky_sampler_, 0, 2);
    descriptor_set_->BindImage(*backend_resources.color_texture, 6);
}

char const* SkyRenderTask::GetName() const
{
    return "RenderSky";
}

void SkyRenderTask::Execute(RenderTaskContext& context)
{
    TransitionRenderImage(context.backend, context.backend_resources.color_texture, gpu::ImageLayout::kShaderReadWrite);
    context.backend.cmd_buffer->BindPipeline(pipeline_);
    context.backend.cmd_buffer->BindDescriptorSet(descriptor_set_);
    context.backend.cmd_buffer->SetRootConstants(context.gpu_scene.skybox_texture_ids.data(),
        sizeof(uint32_t) * context.gpu_scene.skybox_texture_ids.size());
    context.backend.cmd_buffer->Dispatch((context.viewport_width + 15) / 16, (context.viewport_height + 15) / 16, 1);
    context.backend.cmd_buffer->StorageBarrier(context.backend_resources.color_texture);
}
