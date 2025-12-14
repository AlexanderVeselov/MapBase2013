#include "render_new.h"
#include "dx9_interop.h"
#include "rhi.h"
#include "mathlib/vmatrix.h"
#include "bsp_loader.h"

class RenderImpl : public RenderNew
{
public:
    void Init() override;
    void LoadLevel(char const* level_name) override;
    void RenderView(ViewSetup const& view_setup) override;

private:
    std::unique_ptr<rhi::RHI> rhi_;
    std::shared_ptr<rhi::Texture> color_texture_;
    std::shared_ptr<rhi::Texture> depth_texture_;
    std::shared_ptr<rhi::Texture> shared_depth_texture_;
    std::shared_ptr<rhi::GraphicsPipeline> pipeline_;
    std::shared_ptr<rhi::ComputePipeline> copy_depth_pipeline_;
    std::shared_ptr<rhi::Buffer> vertex_buffer_;
    std::shared_ptr<rhi::Buffer> view_proj_buffer_;
};

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
    uint32_t adapter_idx = GetD3D9AdapterIndex();
    rhi_.reset(rhi::CreateRHI(adapter_idx));

    InitSharedTextures(rhi_.get(), color_texture_, shared_depth_texture_);
    depth_texture_ = rhi_->CreateTexture(color_texture_->GetWidth(), color_texture_->GetHeight(),
        rhi::ImageFormat::kR32_Typeless, 1u,
        rhi::TextureBindFlags::kShaderResource | rhi::TextureBindFlags::kDepthStencil);

    char const* vertex_shader_src = R"(
        cbuffer CameraCB : register(b0)
        {
            float4x4 g_view_projection;
        };

        struct VSInput
        {
            float3 position : POSITION;
            float3 color : COLOR0;
        };
        struct VSOutput
        {
            float4 position : SV_POSITION;
            float3 color : COLOR0;
        };
        VSOutput main(VSInput input)
        {
            VSOutput output;
            output.position = mul(float4(input.position, 1.0), g_view_projection);
            output.color = input.color;
            return output;
        }
    )";

    char const* pixel_shader_src = R"(
        struct PSInput
        {
            float4 position : SV_POSITION;
            float3 color : COLOR0;
        };

        float4 main(PSInput input) : SV_TARGET
        {
            return float4(input.color, 1.0);
        }
    )";

    pipeline_ = rhi_->CreateGraphicsPipeline(vertex_shader_src, pixel_shader_src);

    char const* copy_shader_src = R"(
        Texture2D g_input_tex : register(t0);
        RWTexture2D<float4> g_output_tex : register(u0);

        // https://aras-p.info/blog/2009/07/30/encoding-floats-to-rgba-the-final
        float4 EncodeFloatRGBA(float v) {
            float4 enc = float4(1.0, 255.0, 65025.0, 16581375.0) * v;
            enc = frac(enc);
            enc -= enc.yzww * float4(1.0/255.0,1.0/255.0,1.0/255.0,0.0);
            return enc;
        }

        [numthreads(16, 16, 1)]
        void main(uint3 DTid : SV_DispatchThreadID)
        {
            uint2 tex_size;
            g_output_tex.GetDimensions(tex_size.x, tex_size.y);
            if (any(DTid.xy >= tex_size))
                return;
            float depth = g_input_tex.Load(int3(DTid.xy, 0)).r;
            g_output_tex[DTid.xy] = EncodeFloatRGBA(depth);
        }
    )";

    copy_depth_pipeline_ = rhi_->CreateComputePipeline(copy_shader_src);
    copy_depth_pipeline_->BindShaderResource(depth_texture_, 0);
    copy_depth_pipeline_->BindStorageResource(shared_depth_texture_, 0);

    view_proj_buffer_ = rhi_->CreateBuffer(sizeof(VMatrix), rhi::BufferUsage::kDynamic,
        rhi::BufferBindFlags::kConstantBuffer);

    pipeline_->BindConstantBuffer(view_proj_buffer_, 0);
}

void RenderImpl::LoadLevel(char const* level_name)
{
    std::vector<Vertex> cpu_vertices;
    LoadBsp(level_name, cpu_vertices);
    vertex_buffer_ = rhi_->CreateBuffer(sizeof(Vertex) * cpu_vertices.size(), rhi::BufferUsage::kDefault,
        rhi::BufferBindFlags::kVertexBuffer);
    rhi_->UploadBuffer(vertex_buffer_, cpu_vertices.data(), sizeof(Vertex) * cpu_vertices.size());
}

void RenderImpl::RenderView(ViewSetup const& view_setup)
{
    uint32_t viewport_width = color_texture_->GetWidth();
    uint32_t viewport_height = color_texture_->GetHeight();
    rhi_->SetViewport(0, 0, viewport_width, viewport_height);

    VMatrix view_matrix, projection_matrix, view_projection_matrix;
    ComputeViewMatrices(view_setup, &view_matrix, &projection_matrix, &view_projection_matrix);

    void* mapped_data = rhi_->MapBuffer(view_proj_buffer_);
    memcpy(mapped_data, view_projection_matrix.Base(), sizeof(VMatrix));
    rhi_->UnmapBuffer(view_proj_buffer_);

    rhi_->SetRenderTarget(color_texture_, depth_texture_);

    rhi_->ClearColorTexture(color_texture_, 0.0f, 0.5f, 0.5f, 1.0f);
    rhi_->ClearDepthTexture(depth_texture_, 1.0f);
    rhi_->BindGraphicsPipeline(pipeline_);
    rhi_->BindVertexBuffer(vertex_buffer_);
    rhi_->Draw(vertex_buffer_->GetSize() / sizeof(Vertex), 0);

    rhi_->SetRenderTarget(nullptr, nullptr);
    rhi_->BindComputePipeline(copy_depth_pipeline_);
    rhi_->Dispatch((viewport_width + 15) / 16, (viewport_height + 15) / 16, 1);

    DX9_RenderFrame();

    // Flushing is a must!
    rhi_->Flush();
}

RenderNew* GetRenderNewInstance()
{
    static RenderImpl instance;
    return &instance;
}
