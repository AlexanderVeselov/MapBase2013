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
    case ImageFormat::kUnknown:       return DXGI_FORMAT_UNKNOWN;
    case ImageFormat::kRGBA32_Float:  return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case ImageFormat::kRGBA16_Float:  return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case ImageFormat::kRGBA8_SInt:    return DXGI_FORMAT_R8G8B8A8_SINT;
    case ImageFormat::kRGBA8_UInt:    return DXGI_FORMAT_R8G8B8A8_UINT;
    case ImageFormat::kRGBA8_UNorm:   return DXGI_FORMAT_R8G8B8A8_UNORM;
    case ImageFormat::kBGRA8_UNorm:   return DXGI_FORMAT_B8G8R8A8_UNORM;
    case ImageFormat::kRGBA8_SRGB:    return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case ImageFormat::kRG32_Float:    return DXGI_FORMAT_R32G32_FLOAT;
    case ImageFormat::kRG16_Float:    return DXGI_FORMAT_R16G16_FLOAT;
    case ImageFormat::kR32_Float:     return DXGI_FORMAT_R32_FLOAT;
    case ImageFormat::kR32_Typeless:  return DXGI_FORMAT_R32_TYPELESS;
    case ImageFormat::kD32_Float:     return DXGI_FORMAT_D32_FLOAT;
    case ImageFormat::kR16_Float:     return DXGI_FORMAT_R16_FLOAT;
    default:
        assert(!"Unknown ImageFormat");
        return DXGI_FORMAT_UNKNOWN;
    }
}

UINT TextureBindFlagsToD3D11(TextureBindFlags bind_flags)
{
    UINT flags = 0;
    if (HasFlag(bind_flags, TextureBindFlags::kShaderResource))
        flags |= D3D11_BIND_SHADER_RESOURCE;
    if (HasFlag(bind_flags, TextureBindFlags::kRenderTarget))
        flags |= D3D11_BIND_RENDER_TARGET;
    if (HasFlag(bind_flags, TextureBindFlags::kDepthStencil))
        flags |= D3D11_BIND_DEPTH_STENCIL;
    if (HasFlag(bind_flags, TextureBindFlags::kUnorderedAccess))
        flags |= D3D11_BIND_UNORDERED_ACCESS;
    return flags;
}

D3D11_USAGE BufferUsageToD3D11(BufferUsage usage)
{
    switch (usage)
    {
    case BufferUsage::kDefault:
        return D3D11_USAGE_DEFAULT;
    case BufferUsage::kDynamic:
        return D3D11_USAGE_DYNAMIC;
    default:
        assert(!"Unknown BufferUsage");
        return D3D11_USAGE_DEFAULT;
    }
}

UINT BufferBindFlagsToD3D11(BufferBindFlags bind_flags)
{
    UINT flags = 0;
    if (HasFlag(bind_flags, BufferBindFlags::kVertexBuffer))
        flags |= D3D11_BIND_VERTEX_BUFFER;
    if (HasFlag(bind_flags, BufferBindFlags::kIndexBuffer))
        flags |= D3D11_BIND_INDEX_BUFFER;
    if (HasFlag(bind_flags, BufferBindFlags::kConstantBuffer))
        flags |= D3D11_BIND_CONSTANT_BUFFER;
    if (HasFlag(bind_flags, BufferBindFlags::kShaderResource))
        flags |= D3D11_BIND_SHADER_RESOURCE;
    if (HasFlag(bind_flags, BufferBindFlags::kUnorderedAccess))
        flags |= D3D11_BIND_UNORDERED_ACCESS;
    return flags;
}

bool CheckDepthFormat(ImageFormat format)
{
    return format == ImageFormat::kD32_Float || format == ImageFormat::kR32_Typeless;
}

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
            char const* error_srt = (const char*)errors->GetBufferPointer();
            OutputDebugString(error_srt);
            MessageBoxA(nullptr, error_srt, "Shader Compilation Error", MB_OK);
            assert(false);
        }
        return false;
    }
    return true;
}

class D3D11Texture : public Texture
{
public:
    D3D11Texture(ID3D11Device* device, uint32_t width, uint32_t height, ImageFormat format,
        uint32_t mip_levels, TextureBindFlags bind_flags, UINT misc_flags = 0)
        : Texture(width, height, format, mip_levels, bind_flags)
    {
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = width;
        d.Height = height;
        d.MipLevels = mip_levels;
        d.ArraySize = 1;
        d.Format = FormatToDXGI(format);
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = TextureBindFlagsToD3D11(bind_flags);
        d.MiscFlags = misc_flags;

        CheckResult(device->CreateTexture2D(&d, nullptr, &texture_));

        if (HasFlag(bind_flags, TextureBindFlags::kShaderResource))
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Format = (d.Format == DXGI_FORMAT_R32_TYPELESS) ?
                DXGI_FORMAT_R32_FLOAT : d.Format;
            srv_desc.Texture2D.MostDetailedMip = 0;
            srv_desc.Texture2D.MipLevels = d.MipLevels; // CHECK?
            CheckResult(device->CreateShaderResourceView(texture_.Get(), &srv_desc, &srv_));
        }

        if (HasFlag(bind_flags, TextureBindFlags::kRenderTarget))
        {
            CheckResult(device->CreateRenderTargetView(texture_.Get(), nullptr, &rtv_));
        }

        if (HasFlag(bind_flags, TextureBindFlags::kDepthStencil))
        {
            if (!CheckDepthFormat(format))
            {
                assert(!"Invalid format for DepthStencil texture");
            }

            D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
            dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
            dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            CheckResult(device->CreateDepthStencilView(texture_.Get(), &dsv_desc, &dsv_));
        }

        if (HasFlag(bind_flags, TextureBindFlags::kUnorderedAccess))
        {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
            uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            uav_desc.Format = d.Format == DXGI_FORMAT_R32_TYPELESS ?
                DXGI_FORMAT_R32_FLOAT : d.Format;
            CheckResult(device->CreateUnorderedAccessView(texture_.Get(), &uav_desc, &uav_));
        }
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
    ID3D11DepthStencilView* GetDSV() const { return dsv_.Get(); }
    ID3D11UnorderedAccessView* GetUAV() const { return uav_.Get(); }

private:
    ComPtr<ID3D11Texture2D> texture_;
    ComPtr<ID3D11RenderTargetView> rtv_;
    ComPtr<ID3D11ShaderResourceView> srv_;
    ComPtr<ID3D11DepthStencilView> dsv_;
    ComPtr<ID3D11UnorderedAccessView> uav_;
};

class D3D11Buffer : public Buffer
{
public:
    D3D11Buffer(ID3D11Device* device, uint32_t size, BufferUsage usage, BufferBindFlags bind_flags)
        : Buffer(size, usage, bind_flags)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = size;
        desc.Usage = BufferUsageToD3D11(usage);
        desc.BindFlags = BufferBindFlagsToD3D11(bind_flags);
        if (usage == BufferUsage::kDynamic)
        {
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        }
        CheckResult(device->CreateBuffer(&desc, nullptr, &buffer_));
    }

    ID3D11Buffer* GetBuffer() const { return buffer_.Get(); }

private:
    ComPtr<ID3D11Buffer> buffer_;
};

class D3D11GraphicsPipeline : public GraphicsPipeline
{
public:
    D3D11GraphicsPipeline(ID3D11Device* device, char const* vs_src, char const* ps_src)
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
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,                            D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        CheckResult(device->CreateInputLayout(
            input_layout, _countof(input_layout),
            vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(),
            &layout_));

        D3D11_DEPTH_STENCIL_DESC dss_desc = {};
        dss_desc.DepthEnable = TRUE;
        dss_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        dss_desc.DepthFunc = D3D11_COMPARISON_LESS;
        dss_desc.StencilEnable = FALSE;
        CheckResult(device->CreateDepthStencilState(&dss_desc, &depth_stencil_state_));

        D3D11_RASTERIZER_DESC rs = {};
        rs.FillMode = D3D11_FILL_SOLID;
        rs.CullMode = D3D11_CULL_NONE;
        rs.FrontCounterClockwise = FALSE;
        device->CreateRasterizerState(&rs, &rasterizer_state_);
    }

    // Getters
    ID3D11VertexShader* GetVertexShader() const { return vs_.Get(); }
    ID3D11PixelShader* GetPixelShader() const { return ps_.Get(); }
    ID3D11InputLayout* GetInputLayout() const { return layout_.Get(); }
    ID3D11DepthStencilState* GetDepthStencilState() const { return depth_stencil_state_.Get(); }
    ID3D11RasterizerState* GetRasterizerState() const { return rasterizer_state_.Get(); }

private:
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader>  ps_;
    ComPtr<ID3D11InputLayout>  layout_;
    ComPtr<ID3D11DepthStencilState> depth_stencil_state_;
    ComPtr<ID3D11RasterizerState> rasterizer_state_;
};

class D3D11ComputePipeline : public ComputePipeline
{
public:
    D3D11ComputePipeline(ID3D11Device* device, char const* cs_src)
    {
        ComPtr<ID3DBlob> cs_blob;
        CompileShaderFromString(cs_src, "main", "cs_5_0", cs_blob);
        CheckResult(device->CreateComputeShader(
            cs_blob->GetBufferPointer(), cs_blob->GetBufferSize(),
            nullptr, &cs_));
    }
    ID3D11ComputeShader* GetComputeShader() const { return cs_.Get(); }

private:
    ComPtr<ID3D11ComputeShader> cs_;
};

class D3D11RHI : public RHI
{
public:
    D3D11RHI(uint32_t adapter)
    {
        // Create DXGI factory
        CheckResult(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&dxgi_factory_));

        ComPtr<IDXGIAdapter> adapterPtr;
        CheckResult(dxgi_factory_->EnumAdapters(adapter, &adapterPtr));

        D3D_FEATURE_LEVEL featureLevel;
        CheckResult(D3D11CreateDevice(
            adapterPtr.Get(),
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

        if (featureLevel < D3D_FEATURE_LEVEL_11_0) {
            assert(false); // Require D3D11 feature level 11.0 or higher
        }
    }

    std::shared_ptr<Texture> CreateTexture(uint32_t width, uint32_t height, ImageFormat format,
        uint32_t mip_levels, TextureBindFlags bind_flags, UINT misc_flags) override
    {
        return std::make_shared<D3D11Texture>(device_.Get(), width, height, format,
            mip_levels, bind_flags, misc_flags);
    }

    std::shared_ptr<Buffer> CreateBuffer(uint32_t size, BufferUsage usage, BufferBindFlags bind_flags) override
    {
        return std::make_shared<D3D11Buffer>(device_.Get(), size, usage, bind_flags);
    }

    std::shared_ptr<GraphicsPipeline> CreateGraphicsPipeline(char const* vs, char const* ps) override
    {
        return std::make_shared<D3D11GraphicsPipeline>(device_.Get(), vs, ps);
    }

    std::shared_ptr<ComputePipeline> CreateComputePipeline(char const* cs) override
    {
        return std::make_shared<D3D11ComputePipeline>(device_.Get(), cs);
    }

    void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override
    {
        D3D11_VIEWPORT viewport = {};
        viewport.TopLeftX = static_cast<float>(x);
        viewport.TopLeftY = static_cast<float>(y);
        viewport.Width = static_cast<float>(width);
        viewport.Height = static_cast<float>(height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        context_->RSSetViewports(1, &viewport);
    }

    void BindGraphicsPipeline(std::shared_ptr<GraphicsPipeline> pipeline) override
    {
        // Cast to D3D11Pipeline
        auto d3d11_pipeline = std::static_pointer_cast<D3D11GraphicsPipeline>(pipeline);
        context_->IASetInputLayout(d3d11_pipeline->GetInputLayout());
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(d3d11_pipeline->GetVertexShader(), nullptr, 0);
        context_->PSSetShader(d3d11_pipeline->GetPixelShader(), nullptr, 0);
        context_->OMSetDepthStencilState(d3d11_pipeline->GetDepthStencilState(), 0);
        context_->RSSetState(d3d11_pipeline->GetRasterizerState());

        // Bind constant buffers
        for (const auto& buffer : pipeline->GetBoundConstantBuffers())
        {
            uint32_t slot = buffer.first;
            auto d3d11_buffer = std::static_pointer_cast<D3D11Buffer>(buffer.second);
            ID3D11Buffer* buf = d3d11_buffer->GetBuffer();
            assert(buf);
            context_->VSSetConstantBuffers(slot, 1, &buf);
            context_->PSSetConstantBuffers(slot, 1, &buf);
        }

        // Bind shader resources
        for (const auto& resource : pipeline->GetBoundShaderResources())
        {
            uint32_t slot = resource.first;
            auto res = resource.second;
            if (auto tex = std::dynamic_pointer_cast<D3D11Texture>(res))
            {
                ID3D11ShaderResourceView* srv = tex->GetSRV();
                assert(srv);
                context_->VSSetShaderResources(slot, 1, &srv);
                context_->PSSetShaderResources(slot, 1, &srv);
            }
            else if (auto buf = std::dynamic_pointer_cast<D3D11Buffer>(res))
            {
                ID3D11ShaderResourceView* srv = nullptr;
                assert(!"SRV for buffer not implemented");
                // Create SRV for buffer if needed (not implemented here)
                // Bind SRV to VS and PS
                context_->VSSetShaderResources(slot, 1, &srv);
                context_->PSSetShaderResources(slot, 1, &srv);
            }
        }

        // Bind UAVs
        for (const auto& resource : pipeline->GetBoundStorageResources())
        {
            //uint32_t slot = resource.first;
            auto res = resource.second;
            if (auto tex = std::dynamic_pointer_cast<D3D11Texture>(res))
            {
                //ID3D11UnorderedAccessView* uav = nullptr;
                assert(!"UAV textures for graphics pipelines are not implemented");
                // Create UAV for texture if needed (not implemented here)
                //context_->OMSetRenderTargetsAndUnorderedAccessViews(
                //    0, nullptr, nullptr,
                //    slot, 1, &uav, nullptr);
            }
            else if (auto buf = std::dynamic_pointer_cast<D3D11Buffer>(res))
            {
                //ID3D11UnorderedAccessView* uav = nullptr;
                assert(!"UAV buffers for graphics pipelines are not implemented");
                // Create UAV for buffer if needed (not implemented here)
                //context_->OMSetRenderTargetsAndUnorderedAccessViews(
                //    0, nullptr, nullptr,
                //    slot, 1, &uav, nullptr);
            }
        }
    }

    void BindComputePipeline(std::shared_ptr<ComputePipeline> pipeline) override
    {
        auto d3d11_pipeline = std::static_pointer_cast<D3D11ComputePipeline>(pipeline);
        context_->CSSetShader(d3d11_pipeline->GetComputeShader(), nullptr, 0);

        // Bind constant buffers
        for (const auto& buffer : pipeline->GetBoundConstantBuffers())
        {
            uint32_t slot = buffer.first;
            auto d3d11_buffer = std::static_pointer_cast<D3D11Buffer>(buffer.second);
            ID3D11Buffer* buf = d3d11_buffer->GetBuffer();
            assert(buf);
            context_->CSSetConstantBuffers(slot, 1, &buf);
        }

        // Bind shader resources
        for (const auto& resource : pipeline->GetBoundShaderResources())
        {
            uint32_t slot = resource.first;
            auto res = resource.second;
            if (auto tex = std::dynamic_pointer_cast<D3D11Texture>(res))
            {
                ID3D11ShaderResourceView* srv = tex->GetSRV();
                assert(srv);
                context_->CSSetShaderResources(slot, 1, &srv);
            }
            else if (auto buf = std::dynamic_pointer_cast<D3D11Buffer>(res))
            {
                ID3D11ShaderResourceView* srv = nullptr;
                assert(!"SRV for buffer not implemented");
                // Create SRV for buffer if needed (not implemented here)
                // Bind SRV to CS
                context_->CSSetShaderResources(slot, 1, &srv);
            }
        }

        // Bind UAVs
        for (const auto& resource : pipeline->GetBoundStorageResources())
        {
            uint32_t slot = resource.first;
            auto res = resource.second;
            if (auto tex = std::dynamic_pointer_cast<D3D11Texture>(res))
            {
                ID3D11UnorderedAccessView* uav = tex->GetUAV();
                assert(uav);
                context_->CSSetUnorderedAccessViews(slot, 1, &uav, nullptr);
            }
            else if (auto buf = std::dynamic_pointer_cast<D3D11Buffer>(res))
            {
                //ID3D11UnorderedAccessView* uav = nullptr;
                assert(!"UAV buffers for compute pipelines are not implemented");
                // Create UAV for buffer if needed (not implemented here)
                //context_->CSSetUnorderedAccessViews(slot, 1, &uav, nullptr);
            }
        }
    }

    void Draw(uint32_t vertex_count, uint32_t start_vertex) override
    {
        context_->Draw(vertex_count, start_vertex);
    }

    void Dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) override
    {
        context_->Dispatch(group_count_x, group_count_y, group_count_z);
    }

    // TODO: ClearRenderTarget instead of ClearTexture
    void ClearColorTexture(std::shared_ptr<Texture> texture, float r, float g, float b, float a) override
    {
        assert(HasFlag(texture->GetBindFlags(), TextureBindFlags::kRenderTarget) &&
            "ClearColorTexture called on non-render target texture");

        auto d3d11_texture = std::static_pointer_cast<D3D11Texture>(texture);
        ID3D11RenderTargetView* rtv = d3d11_texture->GetRTV();
        assert(rtv && "ClearTexture called on texture without RTV");

        float color[4] = { r, g, b, a };
        context_->ClearRenderTargetView(rtv, color);
    }

    void ClearDepthTexture(std::shared_ptr<Texture> texture, float depth) override
    {
        assert(HasFlag(texture->GetBindFlags(), TextureBindFlags::kDepthStencil) &&
            "ClearDepthTexture called on non-depth stencil texture");

        auto d3d11_texture = std::static_pointer_cast<D3D11Texture>(texture);
        ID3D11DepthStencilView* dsv = d3d11_texture->GetDSV();
        assert(dsv && "ClearTexture called on texture without DSV");

        context_->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, depth, 0);
    }

    void SetRenderTarget(std::shared_ptr<Texture> color, std::shared_ptr<Texture> depth) override
    {
        ID3D11RenderTargetView* rtv[1] = { nullptr };
        if (color)
        {
            assert(HasFlag(color->GetBindFlags(), TextureBindFlags::kRenderTarget) &&
                "SetRenderTarget called on non-render target color texture");
            auto d3d11_color_texture = std::static_pointer_cast<D3D11Texture>(color);
            rtv[0] = d3d11_color_texture->GetRTV();
            assert(rtv[0] && "SetRenderTarget called on texture without RTV");
        }

        ID3D11DepthStencilView* dsv = nullptr;
        if (depth)
        {
            assert(HasFlag(depth->GetBindFlags(), TextureBindFlags::kDepthStencil) &&
                "SetRenderTarget called on non-depth stencil depth texture");
            auto d3d11_depth_texture = std::static_pointer_cast<D3D11Texture>(depth);
            dsv = d3d11_depth_texture->GetDSV();
            assert(dsv && "SetRenderTarget called on texture without DSV");
        }

        context_->OMSetRenderTargets(1, rtv, dsv);
    }

    void UploadBuffer(std::shared_ptr<Buffer> buffer, void const* data, size_t size) override
    {
        auto d3d11_buffer = std::static_pointer_cast<D3D11Buffer>(buffer);
        ID3D11Buffer* buf = d3d11_buffer->GetBuffer();
        assert(buf != nullptr);

        D3D11_BUFFER_DESC desc = {};
        buf->GetDesc(&desc);

        // Basic sanity check
        assert(size <= desc.ByteWidth);

        // Whole-buffer update — fastest path for DEFAULT usage
        if (size == desc.ByteWidth)
        {
            context_->UpdateSubresource(buf, 0, nullptr, data, 0, 0);
            return;
        }

        // Partial update using a box
        D3D11_BOX box{};
        box.left = 0;
        box.right = (UINT)size;
        box.top = 0;
        box.bottom = 1;
        box.front = 0;
        box.back = 1;

        context_->UpdateSubresource(buf, 0, &box, data, 0, 0);
    }

    void* MapBuffer(std::shared_ptr<Buffer> buffer) override
    {
        auto d3d11_buffer = std::static_pointer_cast<D3D11Buffer>(buffer);

        D3D11_MAPPED_SUBRESOURCE mapped_resource = {};
        CheckResult(context_->Map(d3d11_buffer->GetBuffer(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource));
        return mapped_resource.pData;
    }

    void UnmapBuffer(std::shared_ptr<Buffer> buffer) override
    {
        auto d3d11_buffer = std::static_pointer_cast<D3D11Buffer>(buffer);
        context_->Unmap(d3d11_buffer->GetBuffer(), 0);
    }

    void BindVertexBuffer(std::shared_ptr<Buffer> buffer, uint32_t stride) override
    {
        auto d3d11_buffer = std::static_pointer_cast<D3D11Buffer>(buffer);
        ID3D11Buffer* buf = d3d11_buffer->GetBuffer();
        assert(buf != nullptr);
        UINT offset = 0;
        context_->IASetVertexBuffers(0, 1, &buf, &stride, &offset);
	}

    void BindIndexBuffer(std::shared_ptr<Buffer> buffer) override
    {
        auto d3d11_buffer = std::static_pointer_cast<D3D11Buffer>(buffer);
        ID3D11Buffer* buf = d3d11_buffer->GetBuffer();
        assert(buf != nullptr);
        context_->IASetIndexBuffer(buf, DXGI_FORMAT_R32_UINT, 0);
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
