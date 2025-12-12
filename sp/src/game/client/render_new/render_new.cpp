#include "render_new.h"
#include "dx9_interop.h"
#include "rhi.h"

class RenderImpl : public RenderNew
{
public:
    void Init() override;
    void RenderFrame() override;

private:
    std::unique_ptr<rhi::RHI> rhi_;
    std::shared_ptr<rhi::Texture> shared_texture_;
    std::shared_ptr<rhi::Pipeline> pipeline_;
    std::shared_ptr<rhi::Buffer> vertex_buffer_;
};

struct Vertex
{
    float pos[3];
    float color[3];
};


void RenderImpl::Init()
{
    DX9_InitD3D9Interop();
    uint32_t adapter_idx = GetD3D9AdapterIndex();
    rhi_.reset(rhi::CreateRHI(adapter_idx));

    InitSharedTexture(rhi_.get(), shared_texture_);

    char const* vertex_shader_src = R"(
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
            output.position = float4(input.position, 1.0);
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
	vertex_buffer_ = rhi_->CreateBuffer(sizeof(verts));
	rhi_->UploadBuffer(vertex_buffer_, verts, sizeof(verts));
}

void RenderImpl::RenderFrame()
{
	rhi_->SetViewport(0, 0, 1920, 1080);
    rhi_->ClearTexture(shared_texture_, 0.0f, 0.5f, 0.5f, 1.0f);
	rhi_->BindPipeline(pipeline_);
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
