#include "cbase.h"
#include "taa_task.h"

void TaaTask::Initialize(gpu::DevicePtr const& device)
{
    device_ = device.get();
    pipeline_ = device->CreateComputePipeline("taa_resolve.cs");

    gpu::SamplerDesc sampler_desc;
    sampler_desc.min_filter = gpu::SamplerFilter::kLinear;
    sampler_desc.mag_filter = gpu::SamplerFilter::kLinear;
    sampler_desc.address_u = gpu::SamplerAddressMode::kClampToEdge;
    sampler_desc.address_v = gpu::SamplerAddressMode::kClampToEdge;
    history_sampler_ = device->GetSampler(sampler_desc);

    descriptor_set_ = pipeline_->CreateDescriptorSet();
}

void TaaTask::ResetHistory()
{
    has_valid_history_ = false;
}

void TaaTask::EnsureHistoryTexture(RenderTaskContext& context)
{
    uint32_t width = context.backend_resources.scene_color_texture->GetWidth();
    uint32_t height = context.backend_resources.scene_color_texture->GetHeight();
    if (history_texture_
        && history_texture_->GetWidth() == width
        && history_texture_->GetHeight() == height)
    {
        return;
    }

    history_texture_ = device_->CreateImage(width, height, gpu::ImageFormat::kBGRA8_UNorm,
        gpu::ImageFlags::kShaderResource | gpu::ImageFlags::kStorage);
    has_valid_history_ = false;
}

char const* TaaTask::GetName() const
{
    return "TaskTAA";
}

void TaaTask::Execute(RenderTaskContext& context)
{
    EnsureHistoryTexture(context);
    gpu::ImagePtr const& history_input = has_valid_history_
        ? history_texture_
        : context.backend_resources.scene_color_texture;

    TransitionRenderImage(context.backend, context.backend_resources.scene_color_texture, gpu::ImageLayout::kShaderRead);
    TransitionRenderImage(context.backend, context.backend_resources.velocity_texture, gpu::ImageLayout::kShaderRead);
    TransitionRenderImage(context.backend, history_input, gpu::ImageLayout::kShaderRead);
    TransitionRenderImage(context.backend, context.backend_resources.color_texture, gpu::ImageLayout::kShaderReadWrite);

    descriptor_set_->Clear();
    descriptor_set_->BindImage(*context.backend_resources.scene_color_texture, 0);
    descriptor_set_->BindImage(*context.backend_resources.velocity_texture, 1);
    descriptor_set_->BindImage(*history_input, 2);
    descriptor_set_->BindImage(*context.backend_resources.color_texture, 3);
    descriptor_set_->BindSampler(*history_sampler_, 4);

    context.backend.cmd_buffer->BindPipeline(pipeline_);
    context.backend.cmd_buffer->BindDescriptorSet(descriptor_set_);
    context.backend.cmd_buffer->Dispatch((context.viewport_width + 15) / 16, (context.viewport_height + 15) / 16, 1);
    context.backend.cmd_buffer->StorageBarrier(context.backend_resources.color_texture);

    TransitionRenderImage(context.backend, context.backend_resources.color_texture, gpu::ImageLayout::kCopySrc);
    TransitionRenderImage(context.backend, history_texture_, gpu::ImageLayout::kCopyDst);
    context.backend.cmd_buffer->CopyImage(history_texture_, context.backend_resources.color_texture);
    TransitionRenderImage(context.backend, history_texture_, gpu::ImageLayout::kShaderRead);
    has_valid_history_ = true;
}