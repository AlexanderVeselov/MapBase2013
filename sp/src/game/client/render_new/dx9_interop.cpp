#include "dx9_interop.h"
#include "rhi.h"
#include <d3d9.h>
#include <d3d11.h>
#include <cstdint>
#include <cassert>
#include <memory>
#include <d3dcompiler.h>
#include <wrl/client.h>

#define CheckResult(hr) if (FAILED(hr)) { assert(false); }

static constexpr uintptr_t kDeviceRVA = 0x001848FC;
IDirect3DDevice9* g_d3d9_device = nullptr;
IDirect3DSwapChain9* g_d3d9_swapchain = nullptr;
IDirect3DTexture9* g_d3d9_shared_color = nullptr;
IDirect3DTexture9* g_d3d9_shared_depth = nullptr;

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
            o.color = 0;
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

void InitSharedTextures(rhi::RHI* rhi, std::shared_ptr<rhi::Texture>& color_tex,
    std::shared_ptr<rhi::Texture>& depth_tex)
{
    IDirect3DSurface9* pBackBuffer = nullptr;
    CheckResult(g_d3d9_swapchain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer));

    D3DSURFACE_DESC backbuffer_desc;
    pBackBuffer->GetDesc(&backbuffer_desc);

    color_tex = rhi->CreateTexture(
        backbuffer_desc.Width,
        backbuffer_desc.Height,
        rhi::ImageFormat::kBGRA8_UNorm,
        1,
        rhi::TextureBindFlags::kRenderTarget,
        D3D11_RESOURCE_MISC_SHARED
    );

    HANDLE shared_color_handle = (HANDLE)color_tex->GetSharedHandle();

    CheckResult(g_d3d9_device->CreateTexture(
        backbuffer_desc.Width, backbuffer_desc.Height,
        1,
        D3DUSAGE_RENDERTARGET,
        D3DFMT_A8R8G8B8,
        D3DPOOL_DEFAULT,
        &g_d3d9_shared_color,
        &shared_color_handle
    ));

    depth_tex = rhi->CreateTexture(
        backbuffer_desc.Width,
        backbuffer_desc.Height,
        rhi::ImageFormat::kBGRA8_UNorm,
        1,
        rhi::TextureBindFlags::kUnorderedAccess,
        D3D11_RESOURCE_MISC_SHARED
    );

    HANDLE shared_depth_handle = (HANDLE)depth_tex->GetSharedHandle();

    CheckResult(g_d3d9_device->CreateTexture(
        backbuffer_desc.Width, backbuffer_desc.Height,
        1,
        D3DUSAGE_RENDERTARGET,
        D3DFMT_A8R8G8B8,
        D3DPOOL_DEFAULT,
        &g_d3d9_shared_depth,
        &shared_depth_handle
    ));
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
