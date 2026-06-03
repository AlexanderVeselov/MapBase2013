#pragma once

#include "gpu_types.hpp"

namespace gpu
{
class Api;
class Device;
}

void DX9_InitD3D9Interop();
unsigned int GetD3D9AdapterIndex();
gpu::DevicePtr CreateD3D12DeviceForD3D9Adapter(gpu::Api& api);
void InitSharedTextures(gpu::Device& device, gpu::ImagePtr& color_tex, gpu::ImagePtr& depth_tex);
void DX9_RenderFrame();
