#pragma once

#include <memory>

namespace rhi
{
class Texture;
class RHI;
}

void DX9_InitD3D9Interop();
unsigned int GetD3D9AdapterIndex();
void InitSharedTextures(rhi::RHI* rhi, std::shared_ptr<rhi::Texture>& color_tex,
    std::shared_ptr<rhi::Texture>& depth_tex);
void DX9_RenderFrame();
