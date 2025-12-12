#include "render_new.h"
#include "dx9_interop.h"
#include "rhi.h"

rhi::RHI* rhi_;
std::shared_ptr<rhi::Texture> shared_texture_;

void RenderNew::Init()
{
	DX9_InitD3D9Interop();
	uint32_t adapter_idx = GetD3D9AdapterIndex();
	rhi_ = rhi::CreateRHI(adapter_idx);

	InitSharedTexture(rhi_, shared_texture_);
}

void RenderNew::RenderFrame()
{
	rhi_->ClearTexture(shared_texture_, 0.0f, 0.5f, 0.5f, 1.0f);

	DX9_RenderFrame();

	// Flushing is a must!
	rhi_->Flush();
}
