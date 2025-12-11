#include "dx9_interop.h"
#include <d3d9.h>
#include <d3d11.h>
#include <cstdint>
#include <cassert>

static constexpr uintptr_t kDeviceRVA = 0x001848FC;
IDirect3DDevice9* g_d3d9_device = nullptr;
ID3D11Device* g_d3d11_device = nullptr;
IDirect3DSwapChain9* d3d9_swapchain = nullptr;

IDirect3DTexture9* g_d3d9_shared_tex = nullptr;
ID3D11Texture2D* g_d3d11_shared_tex = nullptr;
ID3D11RenderTargetView* g_d3d11_rtv = nullptr;

#define CheckResult(hr) if (FAILED(hr)) { assert(false); }

ID3D11Device* CreateD3D11DeviceFromD3D9(IDirect3DDevice9* d3d9_device)
{
	D3DDEVICE_CREATION_PARAMETERS d3d9_creation_params = {};
	CheckResult(d3d9_device->GetCreationParameters(&d3d9_creation_params));

	// Create DXGI factory
	IDXGIFactory* pDXGIFactory = nullptr;
	CheckResult(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pDXGIFactory));

	// Get DXGI adapter
	IDXGIAdapter* pDXGIAdapter = nullptr;
	CheckResult(pDXGIFactory->EnumAdapters(d3d9_creation_params.AdapterOrdinal, &pDXGIAdapter));

	// Create D3D11 device
	ID3D11DeviceContext* pD3D11DeviceContext = nullptr;
	D3D_FEATURE_LEVEL featureLevel;
	ID3D11Device* d3d11_device = nullptr;
	CheckResult(D3D11CreateDevice(
		pDXGIAdapter,
		D3D_DRIVER_TYPE_UNKNOWN,
		nullptr,
		0,
		nullptr,
		0,
		D3D11_SDK_VERSION,
		&d3d11_device,
		&featureLevel,
		&pD3D11DeviceContext
	));

	return d3d11_device;
}

void DX9_InitD3D9Interop()
{
	HMODULE hShaderApi = GetModuleHandleA("shaderapidx9.dll");
	auto ppDev = reinterpret_cast<IDirect3DDevice9**>(reinterpret_cast<std::uint8_t*>(hShaderApi) + kDeviceRVA);
	g_d3d9_device = *ppDev;
	CheckResult(g_d3d9_device->GetSwapChain(0, &d3d9_swapchain));

	g_d3d11_device = CreateD3D11DeviceFromD3D9(g_d3d9_device);

	IDirect3DSurface9* pBackBuffer = nullptr;
	CheckResult(d3d9_swapchain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer));

	D3DSURFACE_DESC backbuffer_desc;
	pBackBuffer->GetDesc(&backbuffer_desc);

	D3D11_TEXTURE2D_DESC d = {};
	d.Width = backbuffer_desc.Width;
	d.Height = backbuffer_desc.Height;
	d.MipLevels = 1;
	d.ArraySize = 1;
	// TODO: Convert D3DFORMAT to DXGI_FORMAT
	d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_DEFAULT;
	d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	d.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

	CheckResult(g_d3d11_device->CreateTexture2D(&d, nullptr, &g_d3d11_shared_tex));

	CheckResult(g_d3d11_device->CreateRenderTargetView(g_d3d11_shared_tex, nullptr, &g_d3d11_rtv));

	//IDirect3DSurface9* depthstencil9 = nullptr;
	//g_d3d9_device->GetDepthStencilSurface(&depthstencil9);

	IDXGIResource* dxgi_res = nullptr;
	CheckResult(g_d3d11_shared_tex->QueryInterface(&dxgi_res));

	HANDLE shared_handle = nullptr;
	CheckResult(dxgi_res->GetSharedHandle(&shared_handle));

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

	ID3D11DeviceContext* ctx = nullptr;
	g_d3d11_device->GetImmediateContext(&ctx);
	ctx->OMSetRenderTargets(1, &g_d3d11_rtv, nullptr);

	float clear_color[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
	ctx->ClearRenderTargetView(g_d3d11_rtv, clear_color);

	IDirect3DSurface9* d3d9_shared_tex_surface = nullptr;
	g_d3d9_shared_tex->GetSurfaceLevel(0, &d3d9_shared_tex_surface);

	g_d3d9_device->StretchRect(d3d9_shared_tex_surface, nullptr, pBackBuffer, nullptr, D3DTEXF_NONE);

	// Flushing is a must!
	ctx->Flush();
}
