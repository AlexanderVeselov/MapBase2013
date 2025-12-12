#include "dx9_interop.h"
#include "rhi.h"
#include <d3d9.h>
#include <d3d11.h>
#include <cstdint>
#include <cassert>
#include <memory>

#define CheckResult(hr) if (FAILED(hr)) { assert(false); }

static constexpr uintptr_t kDeviceRVA = 0x001848FC;
IDirect3DDevice9* g_d3d9_device = nullptr;
IDirect3DSwapChain9* g_d3d9_swapchain = nullptr;
IDirect3DTexture9* g_d3d9_shared_tex = nullptr;

void DX9_InitD3D9Interop()
{
    HMODULE hShaderApi = GetModuleHandleA("shaderapidx9.dll");
    auto ppDev = reinterpret_cast<IDirect3DDevice9**>(reinterpret_cast<std::uint8_t*>(hShaderApi) + kDeviceRVA);
    g_d3d9_device = *ppDev;
    CheckResult(g_d3d9_device->GetSwapChain(0, &g_d3d9_swapchain));

    //IDirect3DSurface9* depthstencil9 = nullptr;
    //g_d3d9_device->GetDepthStencilSurface(&depthstencil9);
}

unsigned int GetD3D9AdapterIndex()
{
    D3DDEVICE_CREATION_PARAMETERS d3d9_creation_params = {};
    CheckResult(g_d3d9_device->GetCreationParameters(&d3d9_creation_params));
    return d3d9_creation_params.AdapterOrdinal;
}

void InitSharedTexture(rhi::RHI* rhi, std::shared_ptr<rhi::Texture>& shared_tex)
{
    IDirect3DSurface9* pBackBuffer = nullptr;
    CheckResult(g_d3d9_swapchain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer));

    D3DSURFACE_DESC backbuffer_desc;
    pBackBuffer->GetDesc(&backbuffer_desc);

    shared_tex = rhi->CreateTexture(
        backbuffer_desc.Width,
        backbuffer_desc.Height,
        rhi::ImageFormat::kBGRA8_UNorm,
        1,
        D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
        D3D11_RESOURCE_MISC_SHARED
    );

    HANDLE shared_handle = (HANDLE)shared_tex->GetSharedHandle();

    CheckResult(g_d3d9_device->CreateTexture(
        backbuffer_desc.Width, backbuffer_desc.Height,
        1,
        D3DUSAGE_RENDERTARGET,
        D3DFMT_A8R8G8B8,
        D3DPOOL_DEFAULT,
        &g_d3d9_shared_tex,
        &shared_handle
    ));
}

void DX9_RenderFrame()
{
    IDirect3DSurface9* pBackBuffer = nullptr;
    CheckResult(g_d3d9_swapchain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer));

    IDirect3DSurface9* d3d9_shared_tex_surface = nullptr;
    g_d3d9_shared_tex->GetSurfaceLevel(0, &d3d9_shared_tex_surface);

    g_d3d9_device->StretchRect(d3d9_shared_tex_surface, nullptr, pBackBuffer, nullptr, D3DTEXF_NONE);
}
