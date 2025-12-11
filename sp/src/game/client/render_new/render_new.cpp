#include "cbase.h"
#include "render_new.h"
#include "mathlib/vector.h"
#include "../public/shaderapi/ishaderapi.h"
#include "dx9_interop.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

void RenderNew::Init()
{
	DX9_InitD3D9Interop();
}

void RenderNew::RenderFrame()
{
	ConVarRef mat_queue_mode("mat_queue_mode");
	mat_queue_mode.SetValue(0);
	DX9_RenderFrame();
}
