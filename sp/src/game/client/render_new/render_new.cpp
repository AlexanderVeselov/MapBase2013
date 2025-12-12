#include "render_new.h"
#include "dx9_interop.h"
#include "rhi.h"
#include "mathlib/vmatrix.h"

class RenderImpl : public RenderNew
{
public:
    void Init() override;
    void RenderView(ViewSetup const& view_setup) override;

private:
    std::unique_ptr<rhi::RHI> rhi_;
    std::shared_ptr<rhi::Texture> shared_texture_;
    std::shared_ptr<rhi::Pipeline> pipeline_;
    std::shared_ptr<rhi::Buffer> vertex_buffer_;
    std::shared_ptr<rhi::Buffer> view_proj_buffer_;
};

struct Vertex
{
    float pos[3];
    float color[3];
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

    InitSharedTexture(rhi_.get(), shared_texture_);

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
        struct PSInput
        {
            float4 position : SV_POSITION;
            float3 color : COLOR0;
        };
        PSInput main(VSInput input)
        {
            PSInput output;
            output.position = mul(float4(input.position * 1000.0f, 1.0), g_view_projection);
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

    pipeline_ = rhi_->CreatePipeline(vertex_shader_src, pixel_shader_src);

    Vertex verts[] =
    {
        { {  0.0f,  0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
        { {  0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
        { { -0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
    };
    vertex_buffer_ = rhi_->CreateBuffer(sizeof(verts), rhi::BufferUsage::kDefault,
        rhi::BufferBindFlags::kVertexBuffer);
    rhi_->UploadBuffer(vertex_buffer_, verts, sizeof(verts));

    view_proj_buffer_ = rhi_->CreateBuffer(sizeof(VMatrix), rhi::BufferUsage::kDynamic,
        rhi::BufferBindFlags::kConstantBuffer);
}

void RenderImpl::RenderView(ViewSetup const& view_setup)
{
    uint32_t viewport_width = shared_texture_->GetWidth();
    uint32_t viewport_height = shared_texture_->GetHeight();
    rhi_->SetViewport(0, 0, viewport_width, viewport_height);

    VMatrix view_matrix, projection_matrix, view_projection_matrix;
    ComputeViewMatrices(view_setup, &view_matrix, &projection_matrix, &view_projection_matrix);

    void* mapped_data = rhi_->MapBuffer(view_proj_buffer_);
    memcpy(mapped_data, view_projection_matrix.Base(), sizeof(VMatrix));
    rhi_->UnmapBuffer(view_proj_buffer_);

    rhi_->SetRenderTarget(shared_texture_);
    rhi_->ClearTexture(shared_texture_, 0.0f, 0.5f, 0.5f, 1.0f);
    rhi_->BindPipeline(pipeline_);
    rhi_->BindConstantBuffer(view_proj_buffer_, 0);
    rhi_->BindVertexBuffer(vertex_buffer_);
    rhi_->Draw(3, 0);

    DX9_RenderFrame();

    // Flushing is a must!
    rhi_->Flush();
}

RenderNew* GetRenderNewInstance()
{
    static RenderImpl instance;
    return &instance;
}
