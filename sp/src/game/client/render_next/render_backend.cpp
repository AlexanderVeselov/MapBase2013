#include "cbase.h"
#include "render_backend.h"

#include "dx9_interop.h"

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
    RenderBackendResources& resources, RenderSceneGpu& gpu_scene)
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
        gpu::BufferFlags::kCpuAccess | gpu::BufferFlags::kConstant);
    resources.inverse_view_proj_buffer = context.device->CreateBuffer(sizeof(VMatrix), sizeof(VMatrix),
        gpu::BufferFlags::kCpuAccess | gpu::BufferFlags::kConstant);

    SceneTransform identity_transform = MakeIdentitySceneTransform();
    gpu_scene.scene_transform_buffer = context.device->CreateBuffer(sizeof(SceneTransform), sizeof(SceneTransform),
        gpu::BufferFlags::kCpuAccess | gpu::BufferFlags::kShaderResource);
    void* transform_data = gpu_scene.scene_transform_buffer->Map();
    std::memcpy(transform_data, &identity_transform, sizeof(identity_transform));
    gpu_scene.scene_transform_buffer->Unmap();

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

