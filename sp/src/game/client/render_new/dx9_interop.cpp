#include "dx9_interop.h"
#include <d3d9.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstdint>
#include <cassert>
#include <wrl/client.h>
#include <memory>

using namespace Microsoft::WRL;
#define CheckResult(hr) if (FAILED(hr)) { assert(false); }

class Texture
{
public:
    Texture(ID3D11Device* device, uint32_t width, uint32_t height, DXGI_FORMAT format,
        uint32_t mip_levels = 1, UINT bind_flags = 0, UINT misc_flags = 0)
    {
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = width;
        d.Height = height;
        d.MipLevels = mip_levels;
        d.ArraySize = 1;
        d.Format = format;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = bind_flags;
        d.MiscFlags = misc_flags;

        CheckResult(device->CreateTexture2D(&d, nullptr, &texture_));

        // TODO: Create SRV/RTV based on bind_flags
        CheckResult(device->CreateRenderTargetView(texture_.Get(), nullptr, &rtv_));
        CheckResult(device->CreateShaderResourceView(texture_.Get(), nullptr, &srv_));
    }

    HANDLE GetSharedHandle()
    {
        IDXGIResource* dxgi_res = nullptr;
        CheckResult(texture_->QueryInterface(&dxgi_res));

        HANDLE shared_handle = nullptr;
        CheckResult(dxgi_res->GetSharedHandle(&shared_handle));
        dxgi_res->Release();
        return shared_handle;
    }

    ID3D11Texture2D* GetTexture() const { return texture_.Get(); }
    ID3D11RenderTargetView* GetRTV() const { return rtv_.Get(); }
    ID3D11ShaderResourceView* GetSRV() const { return srv_.Get(); }

private:
    ComPtr<ID3D11Texture2D> texture_;
    ComPtr<ID3D11RenderTargetView> rtv_;
    ComPtr<ID3D11ShaderResourceView> srv_;
};

class Pipeline
{
public:
    Pipeline(ID3D11Device* device, char const* vs_src, char const* ps_src)
    {
        ComPtr<ID3DBlob> vs_blob, ps_blob;
        CompileShaderFromString(vs_src, "main", "vs_5_0", vs_blob);
        CompileShaderFromString(ps_src, "main", "ps_5_0", ps_blob);

        CheckResult(device->CreateVertexShader(
            vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(),
            nullptr, &vs_));

        CheckResult(device->CreatePixelShader(
            ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(),
            nullptr, &ps_));

        D3D11_INPUT_ELEMENT_DESC input_layout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,                          D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, sizeof(float) * 3,           D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        CheckResult(device->CreateInputLayout(
            input_layout, _countof(input_layout),
            vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(),
            &layout_));
    }

    // Getters
    ID3D11VertexShader* GetVertexShader() const { return vs_.Get(); }
    ID3D11PixelShader* GetPixelShader() const { return ps_.Get(); }
    ID3D11InputLayout* GetInputLayout() const { return layout_.Get(); }

private:
    bool CompileShaderFromString(char const* src,
        const char* entry,
        const char* target,
        ComPtr<ID3DBlob>& outBlob)
    {
        ComPtr<ID3DBlob> errors;
        HRESULT hr = D3DCompile(
            src, strlen(src),
            nullptr, nullptr, nullptr,
            entry, target,
            D3DCOMPILE_ENABLE_STRICTNESS, 0,
            &outBlob, &errors);

        if (FAILED(hr))
        {
            if (errors)
            {
                //OutputDebugStringA((const char*)errors->GetBufferPointer());
                MessageBoxA(nullptr, (const char*)errors->GetBufferPointer(), "Shader Compilation Error", MB_OK);
            }
            return false;
        }
        return true;
    }

    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader>  ps_;
    ComPtr<ID3D11InputLayout>  layout_;
};


class D3D11Context
{
public:
    D3D11Context(IDirect3DDevice9* d3d9_device)
    {
        D3DDEVICE_CREATION_PARAMETERS d3d9_creation_params = {};
        CheckResult(d3d9_device->GetCreationParameters(&d3d9_creation_params));

        // Create DXGI factory
        CheckResult(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&dxgi_factory_));

        // Get DXGI adapter
        IDXGIAdapter* pDXGIAdapter = nullptr;
        CheckResult(dxgi_factory_->EnumAdapters(d3d9_creation_params.AdapterOrdinal, &pDXGIAdapter));

        // Create D3D11 device
        D3D_FEATURE_LEVEL featureLevel;
        CheckResult(D3D11CreateDevice(
            pDXGIAdapter,
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            0,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &device_,
            &featureLevel,
            &context_
        ));
    }

    std::shared_ptr<Texture> CreateTexture(uint32_t width, uint32_t height, DXGI_FORMAT format,
        uint32_t mip_levels = 1, UINT bind_flags = 0, UINT misc_flags = 0)
    {
        return std::make_shared<Texture>(device_.Get(), width, height, format,
            mip_levels, bind_flags, misc_flags);
    }

    std::shared_ptr<Pipeline> CreatePipeline(char const* vs, char const* ps)
    {
        return std::make_shared<Pipeline>(device_.Get(), vs, ps);
    }

    void BindPipeline(std::shared_ptr<Pipeline> pipeline)
    {
        context_->IASetInputLayout(pipeline->GetInputLayout());
        context_->VSSetShader(pipeline->GetVertexShader(), nullptr, 0);
        context_->PSSetShader(pipeline->GetPixelShader(), nullptr, 0);
    }

    void Draw(uint32_t vertex_count, uint32_t start_vertex)
    {
        context_->Draw(vertex_count, start_vertex);
    }

    void ClearTexture(std::shared_ptr<Texture> texture, float r, float g, float b, float a)
    {
        ID3D11RenderTargetView* rtv = texture->GetRTV();
        context_->OMSetRenderTargets(1, &rtv, nullptr);

        float color[4] = { r, g, b, a };
        context_->ClearRenderTargetView(rtv, color);
    }

    void Flush()
    {
        context_->Flush();
    }

private:
    ComPtr<IDXGIFactory> dxgi_factory_ = nullptr;
    ComPtr<ID3D11Device> device_ = nullptr;
    ComPtr<ID3D11DeviceContext> context_ = nullptr;
};

static constexpr uintptr_t kDeviceRVA = 0x001848FC;
IDirect3DDevice9* g_d3d9_device = nullptr;
IDirect3DSwapChain9* d3d9_swapchain = nullptr;

IDirect3DTexture9* g_d3d9_shared_tex = nullptr;

std::shared_ptr<Texture> g_d3d11_shared_tex = nullptr;
std::unique_ptr<D3D11Context> g_d3d11_context;

void DX9_InitD3D9Interop()
{
    HMODULE hShaderApi = GetModuleHandleA("shaderapidx9.dll");
    auto ppDev = reinterpret_cast<IDirect3DDevice9**>(reinterpret_cast<std::uint8_t*>(hShaderApi) + kDeviceRVA);
    g_d3d9_device = *ppDev;
    CheckResult(g_d3d9_device->GetSwapChain(0, &d3d9_swapchain));

    IDirect3DSurface9* pBackBuffer = nullptr;
    CheckResult(d3d9_swapchain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer));

    D3DSURFACE_DESC backbuffer_desc;
    pBackBuffer->GetDesc(&backbuffer_desc);

    g_d3d11_context.reset(new D3D11Context(g_d3d9_device));

    g_d3d11_shared_tex = g_d3d11_context->CreateTexture(
        backbuffer_desc.Width,
        backbuffer_desc.Height,
        DXGI_FORMAT_B8G8R8A8_UNORM,
        1,
        D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
        D3D11_RESOURCE_MISC_SHARED
    );

    //IDirect3DSurface9* depthstencil9 = nullptr;
    //g_d3d9_device->GetDepthStencilSurface(&depthstencil9);

    HANDLE shared_handle = g_d3d11_shared_tex->GetSharedHandle();

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
    CheckResult(d3d9_swapchain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer));

    g_d3d11_context->ClearTexture(g_d3d11_shared_tex, 0.0f, 0.5f, 0.5f, 1.0f);
    IDirect3DSurface9* d3d9_shared_tex_surface = nullptr;
    g_d3d9_shared_tex->GetSurfaceLevel(0, &d3d9_shared_tex_surface);

    g_d3d9_device->StretchRect(d3d9_shared_tex_surface, nullptr, pBackBuffer, nullptr, D3DTEXF_NONE);

    // Flushing is a must!
    g_d3d11_context->Flush();
}
