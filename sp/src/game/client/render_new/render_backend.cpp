#include "cbase.h"
#include "render_backend.h"

#include "dx9_interop.h"

#include <array>

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

std::array<uint8_t, 16> MakeFallbackTexturePixels()
{
    return {255, 0, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 0, 255, 255};
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

    gpu::GraphicsPipelineDesc pipeline_desc;
    pipeline_desc.vs_filename = "render_new.vs";
    pipeline_desc.ps_filename = "render_new.ps";
    pipeline_desc.color_attachment_formats = {gpu::ImageFormat::kBGRA8_UNorm};
    pipeline_desc.depth_enabled = true;
    pipeline_desc.depth_attachment_format = gpu::ImageFormat::kR32_Typeless;
    resources.pipeline = context.device->CreateGraphicsPipeline(pipeline_desc);
    resources.copy_depth_pipeline = context.device->CreateComputePipeline("copy_depth.cs");

    resources.view_proj_buffer = context.device->CreateBuffer(sizeof(VMatrix), sizeof(VMatrix),
        gpu::BufferFlags::kCpuAccess | gpu::BufferFlags::kConstant);

    gpu::SamplerDesc sampler_desc;
    sampler_desc.min_filter = gpu::SamplerFilter::kLinear;
    sampler_desc.mag_filter = gpu::SamplerFilter::kLinear;
    sampler_desc.address_u = gpu::SamplerAddressMode::kRepeat;
    sampler_desc.address_v = gpu::SamplerAddressMode::kRepeat;
    resources.texture_sampler = context.device->GetSampler(sampler_desc);

    gpu::SamplerDesc lightmap_sampler_desc;
    lightmap_sampler_desc.min_filter = gpu::SamplerFilter::kLinear;
    lightmap_sampler_desc.mag_filter = gpu::SamplerFilter::kLinear;
    lightmap_sampler_desc.address_u = gpu::SamplerAddressMode::kClampToEdge;
    lightmap_sampler_desc.address_v = gpu::SamplerAddressMode::kClampToEdge;
    resources.lightmap_sampler = context.device->GetSampler(lightmap_sampler_desc);

    std::array<uint8_t, 16> fallback_pixels = MakeFallbackTexturePixels();
    resources.fallback_texture = CreateBackendTextureImage(context, 2, 2, fallback_pixels.data(), fallback_pixels.size());
    std::array<uint8_t, 4> fallback_lightmap_pixels = {255, 255, 255, 255};
    resources.fallback_lightmap_texture =
        CreateBackendTextureImage(context, 1, 1, fallback_lightmap_pixels.data(), fallback_lightmap_pixels.size());
    gpu_scene.lightmap_texture = resources.fallback_lightmap_texture;

    SceneTransform identity_transform = MakeIdentitySceneTransform();
    gpu_scene.scene_transform_buffer = context.device->CreateBuffer(sizeof(SceneTransform), sizeof(SceneTransform),
        gpu::BufferFlags::kCpuAccess | gpu::BufferFlags::kShaderResource);
    void* transform_data = gpu_scene.scene_transform_buffer->Map();
    std::memcpy(transform_data, &identity_transform, sizeof(identity_transform));
    gpu_scene.scene_transform_buffer->Unmap();

    resources.pipeline_descriptor_set = resources.pipeline->CreateDescriptorSet();
    resources.pipeline_descriptor_set->BindBuffer(*resources.view_proj_buffer, 0);
    resources.pipeline_descriptor_set->BindBuffer(*gpu_scene.scene_transform_buffer, 1);
    resources.pipeline_descriptor_set->BindSampler(*resources.texture_sampler, 0, 2);
    resources.pipeline_descriptor_set->BindSampler(*resources.lightmap_sampler, 1, 2);
    resources.pipeline_descriptor_set->BindImage(*gpu_scene.lightmap_texture, 0, 3);

    resources.copy_depth_descriptor_set = resources.copy_depth_pipeline->CreateDescriptorSet();
    resources.copy_depth_descriptor_set->BindImage(*resources.depth_texture, 0);
    resources.copy_depth_descriptor_set->BindImage(*resources.shared_depth_texture, 1);
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

gpu::ImagePtr CreateBackendTextureImage(RenderBackendContext& context, uint32_t width, uint32_t height,
    void const* data, size_t data_size)
{
    gpu::ImagePtr image =
        context.device->CreateImage(width, height, gpu::ImageFormat::kRGBA8_UNorm, gpu::ImageFlags::kShaderResource);
    TransitionRenderImage(context, image, gpu::ImageLayout::kCopyDst);
    context.cmd_buffer->UploadImage(image, data, data_size);
    TransitionRenderImage(context, image, gpu::ImageLayout::kShaderRead);
    return image;
}
