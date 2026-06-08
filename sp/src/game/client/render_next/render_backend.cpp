#include "cbase.h"
#include "render_backend.h"

#include "dx9_interop.h"

#include <cstring>

namespace
{
std::string GetShaderDirectory(char const* source_file_path)
{
    std::string file_path = source_file_path;
    size_t last_separator = file_path.find_last_of("\\/");
    if (last_separator == std::string::npos)
    {
        return "shaders";
    }

    return file_path.substr(0, last_separator) + "\\shaders";
}
}

void InitializeRenderBackend(char const* source_file_path, RenderBackendContext& context,
    RenderBackendResources& resources)
{
    DX9_InitD3D9Interop();
    context.api.reset(gpu::Api::Create(gpu::ApiType::kD3D12));

    context.shader_dir = GetShaderDirectory(source_file_path);
    context.api->SetShaderPath(context.shader_dir.c_str());

    context.device = CreateD3D12DeviceForD3D9Adapter(*context.api);
    context.graphics_queue = &context.device->GetQueue(gpu::QueueType::kGraphics);

    InitSharedTextures(*context.device, resources.color_texture, resources.shared_depth_texture);
    resources.depth_texture = context.device->CreateImage(resources.color_texture->GetWidth(), resources.color_texture->GetHeight(),
        gpu::ImageFormat::kR32_Typeless, gpu::ImageFlags::kShaderResource | gpu::ImageFlags::kDepthStencil);

    resources.view_proj_buffer = context.device->CreateBuffer(sizeof(VMatrix), sizeof(VMatrix),
        gpu::BufferFlags::kConstant);
    resources.inverse_view_proj_buffer = context.device->CreateBuffer(sizeof(VMatrix), sizeof(VMatrix),
        gpu::BufferFlags::kConstant);
}

void EnsureRenderCommandBuffer(RenderBackendContext& context)
{
    if (!context.cmd_buffer)
    {
        context.cmd_buffer = context.graphics_queue->CreateCommandBuffer();
    }
}

void TransitionRenderImage(RenderBackendContext& context, gpu::ImagePtr const& image, gpu::ImageLayout desired_layout)
{
    gpu::ImageLayout& current_layout = context.image_layouts[image.get()];
    if (current_layout == desired_layout)
    {
        return;
    }

    EnsureRenderCommandBuffer(context);
    context.cmd_buffer->TransitionBarrier(image, current_layout, desired_layout);
    current_layout = desired_layout;
}

void SubmitRenderCommandsAndWait(RenderBackendContext& context)
{
    if (!context.cmd_buffer)
    {
        return;
    }

    context.graphics_queue->Submit(std::move(context.cmd_buffer));
    context.graphics_queue->WaitIdle();
}

void UploadBufferData(gpu::DevicePtr const& device, gpu::CommandBuffer& cmd_buffer, gpu::BufferPtr& staging_buffer,
    gpu::BufferPtr const& dst_buffer, void const* data, size_t data_size)
{
    if (!dst_buffer || !data || data_size == 0)
    {
        return;
    }

    if (!staging_buffer)
    {
        staging_buffer = device->CreateBuffer(data_size, 1, gpu::BufferFlags::kCpuAccess);
    }
    else if (staging_buffer->GetSize() < data_size)
    {
        staging_buffer->Resize(data_size);
    }

    void* mapped_data = staging_buffer->Map();
    std::memcpy(mapped_data, data, data_size);
    staging_buffer->Unmap();
    cmd_buffer.CopyBuffer(staging_buffer, 0, dst_buffer, 0, data_size);
}
