#include "render_new.h"
#include "dx9_interop.h"


void RenderNew::Init()
{
	DX9_InitD3D9Interop();
}

void RenderNew::RenderFrame()
{
	DX9_RenderFrame();
}
