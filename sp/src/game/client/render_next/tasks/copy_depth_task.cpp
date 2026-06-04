#include "cbase.h"
#include "copy_depth_task.h"

void CopyDepthTask::Initialize(gpu::DevicePtr const& device, RenderBackendResources const& backend_resources)
{
    pipeline_ = device->CreateComputePipeline("copy_depth.cs");
    descriptor_set_ = pipeline_->CreateDescriptorSet();
    descriptor_set_->BindImage(*backend_resources.depth_texture, 0);
    descriptor_set_->BindImage(*backend_resources.shared_depth_texture, 1);
}

char const* CopyDepthTask::GetName() const
{
    return "CopyDepth";
}

void CopyDepthTask::Execute(RenderTaskContext& context)
{
    TransitionRenderImage(context.backend, context.backend_resources.depth_texture, gpu::ImageLayout::kShaderRead);
    TransitionRenderImage(context.backend, context.backend_resources.shared_depth_texture, gpu::ImageLayout::kShaderReadWrite);
    context.backend.cmd_buffer->BindPipeline(pipeline_);
    context.backend.cmd_buffer->BindDescriptorSet(descriptor_set_);
    context.backend.cmd_buffer->Dispatch((context.viewport_width + 15) / 16, (context.viewport_height + 15) / 16, 1);
    context.backend.cmd_buffer->StorageBarrier(context.backend_resources.shared_depth_texture);
}

