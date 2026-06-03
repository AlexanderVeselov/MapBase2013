#include "render_new.h"

#ifdef SOURCE_SDK_RENDER_NEW

#include "dx9_interop.h"
#include "mathlib/vmatrix.h"
#include "bsp_loader.h"

#include "gpu_api.hpp"
#include "gpu_buffer.hpp"
#include "gpu_command_buffer.hpp"
#include "gpu_descriptor_set.hpp"
#include "gpu_device.hpp"
#include "gpu_image.hpp"
#include "gpu_pipeline.hpp"
#include "gpu_queue.hpp"

#include <cstring>
#include <string>
#include <unordered_map>

class RenderImpl : public RenderNew
{
public:
    void Init() override;
    void LoadLevel(char const* level_name) override;
    void RenderView(ViewSetup const& view_setup) override;

private:
    void EnsureCommandBuffer();
    void TransitionImage(gpu::ImagePtr const& image, gpu::ImageLayout desired_layout);
    void SubmitAndWait();

private:
    std::unique_ptr<gpu::Api> api_;
    gpu::DevicePtr device_;
    gpu::Queue* graphics_queue_ = nullptr;
    gpu::CommandBufferPtr cmd_buffer_;
    std::unordered_map<gpu::Image*, gpu::ImageLayout> image_layouts_;
    gpu::ImagePtr color_texture_;
    gpu::ImagePtr depth_texture_;
    gpu::ImagePtr shared_depth_texture_;
    gpu::GraphicsPipelinePtr pipeline_;
    gpu::ComputePipelinePtr copy_depth_pipeline_;
    gpu::DescriptorSetPtr pipeline_descriptor_set_;
    gpu::DescriptorSetPtr copy_depth_descriptor_set_;
    gpu::BufferPtr vertex_buffer_;
    gpu::BufferPtr view_proj_buffer_;
    uint32_t vertex_count_ = 0;
    std::string shader_dir_;
};

namespace
{
std::string GetShaderDirectory()
{
    std::string file_path = __FILE__;
    size_t last_separator = file_path.find_last_of("\\/");
    if (last_separator == std::string::npos)
    {
        return "shaders";
    }

    return file_path.substr(0, last_separator) + "\\shaders";
}
}

void ComputeViewMatrix(VMatrix* pViewMatrix, const Vector& origin, const QAngle& angles)
{
    static VMatrix baseRotation;
    static bool bDidInit;

    if (!bDidInit)
    {
        MatrixBuildRotationAboutAxis(baseRotation, Vector(1, 0, 0), -90);
        MatrixRotate(baseRotation, Vector(0, 0, 1), 90);
        bDidInit = true;
    }

    *pViewMatrix = baseRotation;
    MatrixRotate(*pViewMatrix, Vector(1, 0, 0), -angles[2]);
    MatrixRotate(*pViewMatrix, Vector(0, 1, 0), -angles[0]);
    MatrixRotate(*pViewMatrix, Vector(0, 0, 1), -angles[1]);

    MatrixTranslate(*pViewMatrix, -origin);
}

// Taken from gl_rmain.cpp: 543
void ComputeViewMatrices(ViewSetup const& view_setup, VMatrix* pWorldToView, VMatrix* pViewToProjection, VMatrix* pWorldToProjection)
{
    ComputeViewMatrix(pWorldToView, view_setup.origin, view_setup.angles);
    MatrixBuildPerspectiveX(*pViewToProjection, view_setup.fov, view_setup.m_flAspectRatio,
        view_setup.zNear, view_setup.zFar);
    MatrixMultiply(*pViewToProjection, *pWorldToView, *pWorldToProjection);
}

void RenderImpl::Init()
{
    DX9_InitD3D9Interop();
    api_.reset(gpu::Api::Create(gpu::ApiType::kD3D12));

    shader_dir_ = GetShaderDirectory();
    api_->SetShaderPath(shader_dir_.c_str());

    device_ = CreateD3D12DeviceForD3D9Adapter(*api_);
    graphics_queue_ = &device_->GetQueue(gpu::QueueType::kGraphics);

    InitSharedTextures(*device_, color_texture_, shared_depth_texture_);
    depth_texture_ = device_->CreateImage(color_texture_->GetWidth(), color_texture_->GetHeight(),
        gpu::ImageFormat::kR32_Typeless, gpu::ImageFlags::kShaderResource | gpu::ImageFlags::kDepthStencil);

    gpu::GraphicsPipelineDesc pipeline_desc;
    pipeline_desc.vs_filename = "render_new.vs";
    pipeline_desc.ps_filename = "render_new.ps";
    pipeline_desc.color_attachment_formats = {gpu::ImageFormat::kBGRA8_UNorm};
    pipeline_desc.depth_enabled = true;
    pipeline_desc.depth_attachment_format = gpu::ImageFormat::kR32_Typeless;
    pipeline_ = device_->CreateGraphicsPipeline(pipeline_desc);
    copy_depth_pipeline_ = device_->CreateComputePipeline("copy_depth.cs");

    view_proj_buffer_ = device_->CreateBuffer(sizeof(VMatrix), sizeof(VMatrix),
        gpu::BufferFlags::kCpuAccess | gpu::BufferFlags::kConstant);

    pipeline_descriptor_set_ = pipeline_->CreateDescriptorSet();
    pipeline_descriptor_set_->BindBuffer(*view_proj_buffer_, 0);

    copy_depth_descriptor_set_ = copy_depth_pipeline_->CreateDescriptorSet();
    copy_depth_descriptor_set_->BindImage(*depth_texture_, 0);
    copy_depth_descriptor_set_->BindImage(*shared_depth_texture_, 1);
}

void RenderImpl::LoadLevel(char const* level_name)
{
    std::vector<Vertex> cpu_vertices;
    LoadBsp(level_name, cpu_vertices);
    vertex_count_ = static_cast<uint32_t>(cpu_vertices.size());
    vertex_buffer_ = device_->CreateBuffer(sizeof(Vertex) * cpu_vertices.size(), sizeof(Vertex),
        gpu::BufferFlags::kCpuAccess);

    void* mapped_data = vertex_buffer_->Map();
    std::memcpy(mapped_data, cpu_vertices.data(), sizeof(Vertex) * cpu_vertices.size());
    vertex_buffer_->Unmap();
}

void RenderImpl::RenderView(ViewSetup const& view_setup)
{
    uint32_t viewport_width = color_texture_->GetWidth();
    uint32_t viewport_height = color_texture_->GetHeight();
    EnsureCommandBuffer();
    cmd_buffer_->SetViewport(gpu::Viewport{0.0f, 0.0f, static_cast<float>(viewport_width),
        static_cast<float>(viewport_height), 0.0f, 1.0f});
    cmd_buffer_->SetScissor(gpu::Rect{0, 0, static_cast<int32_t>(viewport_width), static_cast<int32_t>(viewport_height)});

    VMatrix view_matrix, projection_matrix, view_projection_matrix;
    ComputeViewMatrices(view_setup, &view_matrix, &projection_matrix, &view_projection_matrix);

    void* mapped_data = view_proj_buffer_->Map();
    std::memcpy(mapped_data, view_projection_matrix.Base(), sizeof(VMatrix));
    view_proj_buffer_->Unmap();

    TransitionImage(color_texture_, gpu::ImageLayout::kRenderTarget);
    TransitionImage(depth_texture_, gpu::ImageLayout::kRenderTarget);
    cmd_buffer_->SetRenderTarget(color_texture_, depth_texture_);

    cmd_buffer_->ClearImage(color_texture_, 0.0f, 0.5f, 0.5f, 1.0f);
    cmd_buffer_->ClearDepthImage(depth_texture_, 1.0f);
    cmd_buffer_->BindPipeline(pipeline_);
    cmd_buffer_->BindDescriptorSet(pipeline_descriptor_set_);
    cmd_buffer_->SetVertexBuffer(vertex_buffer_, sizeof(Vertex));
    cmd_buffer_->Draw(vertex_count_);

    TransitionImage(depth_texture_, gpu::ImageLayout::kShaderRead);
    TransitionImage(shared_depth_texture_, gpu::ImageLayout::kShaderReadWrite);
    cmd_buffer_->BindPipeline(copy_depth_pipeline_);
    cmd_buffer_->BindDescriptorSet(copy_depth_descriptor_set_);
    cmd_buffer_->Dispatch((viewport_width + 15) / 16, (viewport_height + 15) / 16, 1);
    cmd_buffer_->StorageBarrier(shared_depth_texture_);

    SubmitAndWait();

    DX9_RenderFrame();
}

void RenderImpl::EnsureCommandBuffer()
{
    if (!cmd_buffer_)
    {
        cmd_buffer_ = graphics_queue_->CreateCommandBuffer();
    }
}

void RenderImpl::TransitionImage(gpu::ImagePtr const& image, gpu::ImageLayout desired_layout)
{
    gpu::ImageLayout& current_layout = image_layouts_[image.get()];
    if (current_layout == desired_layout)
    {
        return;
    }

    EnsureCommandBuffer();
    cmd_buffer_->TransitionBarrier(image, current_layout, desired_layout);
    current_layout = desired_layout;
}

void RenderImpl::SubmitAndWait()
{
    if (!cmd_buffer_)
    {
        return;
    }

    graphics_queue_->Submit(std::move(cmd_buffer_));
    graphics_queue_->WaitIdle();
}

RenderNew* GetRenderNewInstance()
{
    static RenderImpl instance;
    return &instance;
}

#else

class RenderNull : public RenderNew
{
public:
    void Init() override {}
    void LoadLevel(char const* level_name) override {}
    void RenderView(ViewSetup const& view_setup) override {}
};

RenderNew* GetRenderNewInstance()
{
    static RenderNull instance;
    return &instance;
}

#endif
