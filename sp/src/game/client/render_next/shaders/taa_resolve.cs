Texture2D<float4> g_current_color : register(t0);
Texture2D<float2> g_velocity : register(t1);
Texture2D<float4> g_history_color : register(t2);
RWTexture2D<float4> g_output_color : register(u3);
SamplerState g_history_sampler : register(s4);

static const float kHistoryWeight = 0.7f;

float3 SampleCurrentClamped(int2 pixel, uint2 texture_size)
{
    int2 clamped_pixel = clamp(pixel, int2(0, 0), int2(texture_size) - 1);
    return g_current_color.Load(int3(clamped_pixel, 0)).rgb;
}

float3 ReinhardToneMap(float3 color)
{
    return color / (color + float3(1.0f, 1.0f, 1.0f));
}

float3 InverseReinhardToneMap(float3 color)
{
    return color / (float3(1.0f, 1.0f, 1.0f) - color);
}

float3 AccumulateHistory(float3 current, float3 history)
{
    float3 current_tone_mapped = ReinhardToneMap(current);
    float3 history_tone_mapped = ReinhardToneMap(history);
    float3 accumulated_tone_mapped = lerp(current_tone_mapped, history_tone_mapped, kHistoryWeight);
    return InverseReinhardToneMap(accumulated_tone_mapped);
}

[numthreads(16, 16, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint2 texture_size;
    g_output_color.GetDimensions(texture_size.x, texture_size.y);
    if (any(dispatch_thread_id.xy >= texture_size))
    {
        return;
    }

    int2 pixel = int2(dispatch_thread_id.xy);
    float4 current_sample = g_current_color.Load(int3(pixel, 0));
    float2 uv = (float2(dispatch_thread_id.xy) + 0.5f) / float2(texture_size);
    float2 velocity = g_velocity.Load(int3(pixel, 0));
    float2 history_uv = uv - velocity;
    bool history_valid = all(history_uv >= float2(0.0f, 0.0f)) && all(history_uv <= float2(1.0f, 1.0f));

    float3 neighborhood_min = current_sample.rgb;
    float3 neighborhood_max = current_sample.rgb;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float3 neighbor = SampleCurrentClamped(pixel + int2(x, y), texture_size);
            neighborhood_min = min(neighborhood_min, neighbor);
            neighborhood_max = max(neighborhood_max, neighbor);
        }
    }

    float4 resolved = current_sample;
    if (history_valid)
    {
        float3 history_sample = g_history_color.SampleLevel(g_history_sampler, history_uv, 0.0f).rgb;
        history_sample = clamp(history_sample, neighborhood_min, neighborhood_max);
        resolved.rgb = AccumulateHistory(current_sample.rgb, history_sample);
    }

    g_output_color[dispatch_thread_id.xy] = resolved;
}
