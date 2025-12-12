#include "rhi.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <cassert>
#include <d3dcompiler.h>

using namespace Microsoft::WRL;
#define CheckResult(hr) if (FAILED(hr)) { assert(false); }

namespace rhi
{
DXGI_FORMAT FormatToDXGI(ImageFormat format)
{
    switch (format)
    {
    case ImageFormat::kRGBA32_Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case ImageFormat::kRGBA16_Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case ImageFormat::kRGBA8_SInt:    return DXGI_FORMAT_R8G8B8A8_SINT;
    case ImageFormat::kRGBA8_UInt:    return DXGI_FORMAT_R8G8B8A8_UINT;
	case ImageFormat::kBGRA8_UNorm:   return DXGI_FORMAT_B8G8R8A8_UNORM;
    case ImageFormat::kRGBA8_UNorm:   return DXGI_FORMAT_R8G8B8A8_UNORM;
    case ImageFormat::kRGBA8_SRGB:    return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

class D3D11Texture : public Texture
{
public:
    D3D11Texture(ID3D11Device* device, uint32_t width, uint32_t height, ImageFormat format,
        uint32_t mip_levels = 1, UINT bind_flags = 0, UINT misc_flags = 0)
    {
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = width;
        d.Height = height;
        d.MipLevels = mip_levels;
        d.ArraySize = 1;
        d.Format = FormatToDXGI(format);
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = bind_flags;
        d.MiscFlags = misc_flags;

        CheckResult(device->CreateTexture2D(&d, nullptr, &texture_));

        // TODO: Create SRV/RTV based on bind_flags
        CheckResult(device->CreateRenderTargetView(texture_.Get(), nullptr, &rtv_));
        CheckResult(device->CreateShaderResourceView(texture_.Get(), nullptr, &srv_));
    }

	void* GetSharedHandle() const override
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

class D3D11Pipeline : public Pipeline
{
public:
    D3D11Pipeline(ID3D11Device* device, char const* vs_src, char const* ps_src)
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

class D3D11RHI : public RHI
{
public:
    D3D11RHI(uint32_t adapter)
    {
        // Create DXGI factory
        CheckResult(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&dxgi_factory_));

        // Get DXGI adapter
        IDXGIAdapter* pDXGIAdapter = nullptr;
        CheckResult(dxgi_factory_->EnumAdapters(adapter, &pDXGIAdapter));

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

    std::shared_ptr<Texture> CreateTexture(uint32_t width, uint32_t height, ImageFormat format,
        uint32_t mip_levels = 1, UINT bind_flags = 0, UINT misc_flags = 0) override
    {
        return std::make_shared<D3D11Texture>(device_.Get(), width, height, format,
            mip_levels, bind_flags, misc_flags);
    }

    std::shared_ptr<Pipeline> CreatePipeline(char const* vs, char const* ps) override
    {
        return std::make_shared<D3D11Pipeline>(device_.Get(), vs, ps);
    }

    void BindPipeline(std::shared_ptr<Pipeline> pipeline) override
    {
        // Cast to D3D11Pipeline
        auto d3d11_pipeline = std::static_pointer_cast<D3D11Pipeline>(pipeline);
        context_->IASetInputLayout(d3d11_pipeline->GetInputLayout());
        context_->VSSetShader(d3d11_pipeline->GetVertexShader(), nullptr, 0);
        context_->PSSetShader(d3d11_pipeline->GetPixelShader(), nullptr, 0);
    }

    void Draw(uint32_t vertex_count, uint32_t start_vertex) override
    {
        context_->Draw(vertex_count, start_vertex);
    }

    void ClearTexture(std::shared_ptr<Texture> texture, float r, float g, float b, float a) override
    {
        auto d3d11_texture = std::static_pointer_cast<D3D11Texture>(texture);
        ID3D11RenderTargetView* rtv = d3d11_texture->GetRTV();
        context_->OMSetRenderTargets(1, &rtv, nullptr);

        float color[4] = { r, g, b, a };
        context_->ClearRenderTargetView(rtv, color);
    }

    void Flush() override
    {
        context_->Flush();
    }

private:
    ComPtr<IDXGIFactory> dxgi_factory_ = nullptr;
    ComPtr<ID3D11Device> device_ = nullptr;
    ComPtr<ID3D11DeviceContext> context_ = nullptr;
};

RHI* CreateRHI(uint32_t adapter)
{
    return new D3D11RHI(adapter);
}

} // namespace rhi
