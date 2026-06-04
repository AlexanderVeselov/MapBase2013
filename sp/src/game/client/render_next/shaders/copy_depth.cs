Texture2D g_input_tex : register(t0);
RWTexture2D<float4> g_output_tex : register(u1);

float4 EncodeFloatRGBA(float v)
{
    if (v == 1.0f)
    {
        return float4(1.0f, 0.0f, 0.0f, 0.0f);
    }

    float4 enc = float4(1.0, 255.0, 65025.0, 16581375.0) * v;
    enc = frac(enc);
    enc -= enc.yzww * float4(1.0 / 255.0, 1.0 / 255.0, 1.0 / 255.0, 0.0);
    return enc;
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

    float depth = g_input_tex.Load(int3(dispatch_thread_id.xy, 0)).r;
    g_output_tex[dispatch_thread_id.xy] = EncodeFloatRGBA(depth);
}
