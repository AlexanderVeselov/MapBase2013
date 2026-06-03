#include "dx9_interop.h"

#include "d3d12_api.hpp"
#include "d3d12_device.hpp"
#include "d3d12_image.hpp"
#include "gpu_device.hpp"

#include <d3d9.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <cassert>
#include <cstdint>
#include <cstring>

#include <wrl/client.h>

#define CheckResult(hr) if (FAILED(hr)) { assert(false); }

static constexpr uintptr_t kDeviceRVA = 0x001848FC;
IDirect3DDevice9* g_d3d9_device = nullptr;
IDirect3DSwapChain9* g_d3d9_swapchain = nullptr;
IDirect3DTexture9* g_d3d9_shared_color = nullptr;
IDirect3DTexture9* g_d3d9_shared_depth = nullptr;
Microsoft::WRL::ComPtr<ID3D11Device> g_interop_device;

namespace
{
IDirect3DVertexShader9* g_vsDepthWrite = nullptr;
IDirect3DPixelShader9* g_psDepthWrite = nullptr;
IDirect3DVertexDeclaration9* g_declDepthWrite = nullptr;

struct QuadV
{
    float x, y, z, w;  // clip space
    float u, v;
};

UINT ToD3D11BindFlags(gpu::ImageFlags flags)
{
    UINT bind_flags = 0;
    if (gpu::HasFlag(flags, gpu::ImageFlags::kRenderTarget))
    {
        bind_flags |= D3D11_BIND_RENDER_TARGET;
    }
    if (gpu::HasFlag(flags, gpu::ImageFlags::kDepthStencil))
    {
        bind_flags |= D3D11_BIND_DEPTH_STENCIL;
    }
    if (gpu::HasFlag(flags, gpu::ImageFlags::kShaderResource))
    {
        bind_flags |= D3D11_BIND_SHADER_RESOURCE;
    }
    if (gpu::HasFlag(flags, gpu::ImageFlags::kStorage))
    {
        bind_flags |= D3D11_BIND_UNORDERED_ACCESS;
    }
    return bind_flags;
}

void EnsureInteropDevice(unsigned int adapter_index)
{
    if (g_interop_device)
    {
        return;
    }

    Microsoft::WRL::ComPtr<IDXGIFactory1> dxgi_factory;
    CheckResult(CreateDXGIFactory1(IID_PPV_ARGS(&dxgi_factory)));

    Microsoft::WRL::ComPtr<IDXGIAdapter1> dxgi_adapter;
    CheckResult(dxgi_factory->EnumAdapters1(adapter_index, &dxgi_adapter));

    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
    CheckResult(D3D11CreateDevice(dxgi_adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, &feature_level, 1,
        D3D11_SDK_VERSION, &g_interop_device, nullptr, nullptr));
}

gpu::ImagePtr CreateSharedImage(gpu::Device& device, uint32_t width, uint32_t height, gpu::ImageFormat format,
    gpu::ImageFlags flags, IDirect3DTexture9** d3d9_texture)
{
    auto* d3d12_device = static_cast<gpu::D3D12Device*>(&device);

    D3D11_TEXTURE2D_DESC texture_desc = {};
    texture_desc.Width = width;
    texture_desc.Height = height;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = ToD3D11BindFlags(flags);
    texture_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> interop_texture;
    CheckResult(g_interop_device->CreateTexture2D(&texture_desc, nullptr, &interop_texture));

    Microsoft::WRL::ComPtr<IDXGIResource> dxgi_resource;
    CheckResult(interop_texture.As(&dxgi_resource));

    HANDLE shared_handle = nullptr;
    CheckResult(dxgi_resource->GetSharedHandle(&shared_handle));

    CheckResult(g_d3d9_device->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
        D3DPOOL_DEFAULT, d3d9_texture, &shared_handle));

    Microsoft::WRL::ComPtr<ID3D12Resource> d3d12_resource;
    CheckResult(d3d12_device->GetD3D12Device()->OpenSharedHandle(shared_handle, IID_PPV_ARGS(&d3d12_resource)));

    return std::make_shared<gpu::D3D12Image>(*d3d12_device, d3d12_resource.Get(), width, height, format, 1, 1,
        flags);
}

void InitDepthWriteShaders()
{
    if (g_vsDepthWrite && g_psDepthWrite && g_declDepthWrite)
        return;

    // Fullscreen triangle/quad in clip space
    const char* vsSrc = R"(
        struct VSIn  { float4 pos : POSITION; float2 uv : TEXCOORD0; };
        struct VSOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };
        VSOut main(VSIn i) { VSOut o; o.pos = i.pos; o.uv = i.uv; return o; }
    )";

    // Write to depth only
    const char* psSrc = R"(
        sampler2D s0 : register(s0);

        struct PSIn { float2 uv : TEXCOORD0; };

        struct PSOut
        {
            float4 color : COLOR0;
            float  depth : DEPTH;
        };

        // https://aras-p.info/blog/2009/07/30/encoding-floats-to-rgba-the-final
        float DecodeFloatRGBA(float4 rgba)
        {
            return rgba.x + rgba.y / 255.0f + rgba.z / 65025.0f + rgba.w / 16581375.0f;
        }

        PSOut main(PSIn i)
        {
            PSOut o;
            float4 encoded_depth = tex2D(s0, i.uv);
            o.color = 0.0f;//DecodeFloatRGBA(encoded_depth);
            o.depth = DecodeFloatRGBA(encoded_depth);
            return o;
        }
    )";

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob, psBlob, err;

    HRESULT hr = D3DCompile(vsSrc, strlen(vsSrc), nullptr, nullptr, nullptr,
        "main", "vs_3_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &err);
    CheckResult(hr);

    hr = D3DCompile(psSrc, strlen(psSrc), nullptr, nullptr, nullptr,
        "main", "ps_3_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &err);
    CheckResult(hr);

    CheckResult(g_d3d9_device->CreateVertexShader((DWORD*)vsBlob->GetBufferPointer(), &g_vsDepthWrite));
    CheckResult(g_d3d9_device->CreatePixelShader((DWORD*)psBlob->GetBufferPointer(), &g_psDepthWrite));

    // Vertex decl: float4 POSITION + float2 TEXCOORD0
    D3DVERTEXELEMENT9 decl[] =
    {
        {0, 0,  D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        D3DDECL_END()
    };
    CheckResult(g_d3d9_device->CreateVertexDeclaration(decl, &g_declDepthWrite));
}

void WriteDepthFromSharedTexture(IDirect3DTexture9* depthTex)
{
    assert(depthTex && g_vsDepthWrite && g_psDepthWrite && g_declDepthWrite);

    // Current depth/stencil surface
    IDirect3DSurface9* depthSurf = nullptr;
    CheckResult(g_d3d9_device->GetDepthStencilSurface(&depthSurf));
    if (!depthSurf) return;

    IDirect3DSurface9* rt0 = nullptr;
    CheckResult(g_d3d9_device->GetRenderTarget(0, &rt0));
    if (!rt0)
    {
        depthSurf->Release();
        return;
    }

    IDirect3DStateBlock9* stateBlock = nullptr;
    CheckResult(g_d3d9_device->CreateStateBlock(D3DSBT_ALL, &stateBlock));

    // Capture current render state
    CheckResult(stateBlock->Capture());

    // Setup render state
    CheckResult(g_d3d9_device->SetVertexDeclaration(g_declDepthWrite));
    CheckResult(g_d3d9_device->SetVertexShader(g_vsDepthWrite));
    CheckResult(g_d3d9_device->SetPixelShader(g_psDepthWrite));

    CheckResult(g_d3d9_device->SetTexture(0, depthTex));

    CheckResult(g_d3d9_device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT));
    CheckResult(g_d3d9_device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT));
    CheckResult(g_d3d9_device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE));
    CheckResult(g_d3d9_device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP));
    CheckResult(g_d3d9_device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP));
    CheckResult(g_d3d9_device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE));

    CheckResult(g_d3d9_device->SetRenderState(D3DRS_ZENABLE, TRUE));
    CheckResult(g_d3d9_device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE));
    CheckResult(g_d3d9_device->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS));

    CheckResult(g_d3d9_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE));
    CheckResult(g_d3d9_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));

    // Disable color writes
    CheckResult(g_d3d9_device->SetRenderState(D3DRS_COLORWRITEENABLE, 0));

    // Set depth/stencil
    CheckResult(g_d3d9_device->SetDepthStencilSurface(depthSurf));

    D3DSURFACE_DESC depth_desc = {};
    CheckResult(depthSurf->GetDesc(&depth_desc));

    D3DVIEWPORT9 viewport = {};
    viewport.X = 0;
    viewport.Y = 0;
    viewport.Width = depth_desc.Width;
    viewport.Height = depth_desc.Height;
    viewport.MinZ = 0.0f;
    viewport.MaxZ = 1.0f;
    CheckResult(g_d3d9_device->SetViewport(&viewport));

    RECT scissor_rect = {0, 0, static_cast<LONG>(depth_desc.Width), static_cast<LONG>(depth_desc.Height)};
    CheckResult(g_d3d9_device->SetScissorRect(&scissor_rect));

    // Fullscreen quad
    QuadV v[6] =
    {
        {-1.f, -1.f, 0.f, 1.f, 0.f, 1.f},
        { 1.f, -1.f, 0.f, 1.f, 1.f, 1.f},
        { 1.f,  1.f, 0.f, 1.f, 1.f, 0.f},

        {-1.f, -1.f, 0.f, 1.f, 0.f, 1.f},
        { 1.f,  1.f, 0.f, 1.f, 1.f, 0.f},
        {-1.f,  1.f, 0.f, 1.f, 0.f, 0.f},
    };

    CheckResult(g_d3d9_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(QuadV)));

    // --- Restore all state exactly as it was ---
    CheckResult(stateBlock->Apply());
    stateBlock->Release();

    rt0->Release();
    depthSurf->Release();
}
}

void DX9_InitD3D9Interop()
{
    HMODULE hShaderApi = GetModuleHandleA("shaderapidx9.dll");
    auto ppDev = reinterpret_cast<IDirect3DDevice9**>(reinterpret_cast<std::uint8_t*>(hShaderApi) + kDeviceRVA);
    g_d3d9_device = *ppDev;
    CheckResult(g_d3d9_device->GetSwapChain(0, &g_d3d9_swapchain));

    InitDepthWriteShaders();
}

unsigned int GetD3D9AdapterIndex()
{
    D3DDEVICE_CREATION_PARAMETERS d3d9_creation_params = {};
    CheckResult(g_d3d9_device->GetCreationParameters(&d3d9_creation_params));
    return d3d9_creation_params.AdapterOrdinal;
}

gpu::DevicePtr CreateD3D12DeviceForD3D9Adapter(gpu::Api& api)
{
    return static_cast<gpu::D3D12Api&>(api).CreateDevice(GetD3D9AdapterIndex());
}

void InitSharedTextures(gpu::Device& device, gpu::ImagePtr& color_tex, gpu::ImagePtr& depth_tex)
{
    EnsureInteropDevice(GetD3D9AdapterIndex());

    Microsoft::WRL::ComPtr<IDirect3DSurface9> backbuffer;
    CheckResult(g_d3d9_swapchain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &backbuffer));

    D3DSURFACE_DESC backbuffer_desc = {};
    CheckResult(backbuffer->GetDesc(&backbuffer_desc));

    color_tex = CreateSharedImage(device, backbuffer_desc.Width, backbuffer_desc.Height, gpu::ImageFormat::kBGRA8_UNorm,
        gpu::ImageFlags::kRenderTarget, &g_d3d9_shared_color);

    depth_tex = CreateSharedImage(device, backbuffer_desc.Width, backbuffer_desc.Height, gpu::ImageFormat::kBGRA8_UNorm,
        gpu::ImageFlags::kStorage | gpu::ImageFlags::kShaderResource, &g_d3d9_shared_depth);
}

void DX9_RenderFrame()
{
    IDirect3DSurface9* pBackBuffer = nullptr;
    CheckResult(g_d3d9_swapchain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer));

    IDirect3DSurface9* d3d9_shared_color_surface = nullptr;
    CheckResult(g_d3d9_shared_color->GetSurfaceLevel(0, &d3d9_shared_color_surface));

    //IDirect3DSurface9* d3d9_shared_depth_surface = nullptr;
    //CheckResult(g_d3d9_shared_depth->GetSurfaceLevel(0, &d3d9_shared_depth_surface));

    //IDirect3DSurface9* d3d9_depth = nullptr;
    //CheckResult(g_d3d9_device->GetDepthStencilSurface(&d3d9_depth));
    CheckResult(g_d3d9_device->StretchRect(d3d9_shared_color_surface, nullptr, pBackBuffer, nullptr, D3DTEXF_NONE));
    WriteDepthFromSharedTexture(g_d3d9_shared_depth);
}
