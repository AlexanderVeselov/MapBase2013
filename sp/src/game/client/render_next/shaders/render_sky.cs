cbuffer SkyCamera : register(b0)
{
    float4x4 g_inverse_view_proj;
};

static const uint RENDER_NEXT_MAX_TEXTURES = 512;

Texture2D g_textures[RENDER_NEXT_MAX_TEXTURES] : register(t0, space1);
StructuredBuffer<uint> g_sky_texture_ids : register(t1);
SamplerState g_sky_sampler : register(s0, space2);
RWTexture2D<float4> g_output_tex : register(u6);

float2 ComputeFaceUv(float3 direction, uint face_index)
{
    float2 uv = 0.0f;

    if (face_index == 0)
    {
        uv = float2(-direction.y, -direction.z) / abs(direction.x);
        uv.y = uv.y * 2.0f + 1.0f;
    }
    else if (face_index == 1)
    {
        uv = float2(direction.y, -direction.z) / abs(direction.x);
        uv.y = uv.y * 2.0f + 1.0f;
    }
    else if (face_index == 2)
    {
        uv = float2(direction.x, -direction.z) / abs(direction.y);
        uv.y = uv.y * 2.0f + 1.0f;
    }
    else if (face_index == 3)
    {
        uv = float2(-direction.x, -direction.z) / abs(direction.y);
        uv.y = uv.y * 2.0f + 1.0f;
    }
    else if (face_index == 4)
    {
        uv = float2(-direction.y, direction.x) / abs(direction.z);
    }
    else
    {
        uv = float2(-direction.y, -direction.x) / abs(direction.z);
    }

    return uv * 0.5f + 0.5f;
}

uint SelectFace(float3 direction)
{
    float3 abs_direction = abs(direction);
    if (abs_direction.x >= abs_direction.y && abs_direction.x >= abs_direction.z)
    {
        return direction.x >= 0.0f ? 0 : 1;
    }

    if (abs_direction.y >= abs_direction.x && abs_direction.y >= abs_direction.z)
    {
        return direction.y >= 0.0f ? 2 : 3;
    }

    return direction.z >= 0.0f ? 4 : 5;
}

[numthreads(16, 16, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint2 texture_size;
    g_output_tex.GetDimensions(texture_size.x, texture_size.y);
    if (any(dispatch_thread_id.xy >= texture_size))
    {
        return;
    }

    float2 uv = (float2(dispatch_thread_id.xy) + 0.5f) / float2(texture_size);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

    float4 near_clip = float4(ndc, 0.0f, 1.0f);
    float4 far_clip = float4(ndc, 1.0f, 1.0f);

    float4 near_world_h = mul(near_clip, g_inverse_view_proj);
    float4 far_world_h = mul(far_clip, g_inverse_view_proj);
    float3 near_world = near_world_h.xyz / max(near_world_h.w, 1e-6f);
    float3 far_world = far_world_h.xyz / max(far_world_h.w, 1e-6f);
    float3 direction = normalize(far_world - near_world);

    uint face_index = SelectFace(direction);
    float2 face_uv = saturate(ComputeFaceUv(direction, face_index));
    uint texture_index = g_sky_texture_ids[face_index];
    g_output_tex[dispatch_thread_id.xy] = g_textures[NonUniformResourceIndex(texture_index)].SampleLevel(g_sky_sampler, face_uv, 0.0f) * 0.75f;
}

