cbuffer CameraCB : register(b0)
{
    float4x4 g_view_projection;
    float4x4 g_prev_view_projection;
    float4 g_camera_jitter;
};

cbuffer g_RootConstants : register(b1)
{
    uint g_draw_instance_id;
};

StructuredBuffer<float4x4> g_scene_transforms : register(t1);

struct InstanceData
{
    float4 color;
    uint vertex_offset;
    uint index_offset;
    uint index_count;
    uint material_index;
    uint transform_index;
    uint vertex_color_offset;
    uint ambient_cube_offset;
    uint bone_offset;
    uint bone_count;
    uint is_visible;
    uint padding;
    uint padding1;
};

StructuredBuffer<InstanceData> g_scene_instances : register(t2);
StructuredBuffer<float4> g_scene_vertex_colors : register(t3);

StructuredBuffer<float4x4> g_scene_bones : register(t4);
StructuredBuffer<float4> g_scene_ambient_cubes : register(t5);
StructuredBuffer<float4x4> g_scene_prev_transforms : register(t6);

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float2 lightmap_texcoord : TEXCOORD1;
    float4 bone_weights : BLENDWEIGHT;
    uint4 bone_indices : BLENDINDICES;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float2 lightmap_texcoord : TEXCOORD1;
    uint material_index : TEXCOORD2;
    float3 color : TEXCOORD3;
    float4 curr_clip_position : TEXCOORD4;
    float4 prev_clip_position : TEXCOORD5;
    float3 world_position : TEXCOORD6;
};

float3 VertexShaderAmbientLight(const float3 worldNormal, uint ambient_cube_offset)
{
    float3 nSquared = worldNormal * worldNormal;
    int3 isNegative = (worldNormal < 0.0f);
    float3 ambientCube[6];
    [unroll]
    for (uint face_index = 0; face_index < 6; ++face_index)
    {
        ambientCube[face_index] = g_scene_ambient_cubes[ambient_cube_offset + face_index].rgb;
    }

    return nSquared.x * ambientCube[isNegative.x]
        + nSquared.y * ambientCube[isNegative.y + 2]
        + nSquared.z * ambientCube[isNegative.z + 4];
}

VSOutput main(VSInput input, uint vertex_id : SV_VertexID)
{
    VSOutput output;
    InstanceData instance_data = g_scene_instances[g_draw_instance_id];

    float3 world_position_xyz;
    float3 world_normal;
    bool has_skinned_motion = instance_data.bone_count > 0 && instance_data.bone_offset != 0xFFFFFFFFu;
    if (instance_data.bone_count > 0 && instance_data.bone_offset != 0xFFFFFFFFu)
    {
        world_position_xyz = float3(0.0f, 0.0f, 0.0f);
        world_normal = float3(0.0f, 0.0f, 0.0f);
        [unroll]
        for (uint influence_index = 0; influence_index < 4; ++influence_index)
        {
            float bone_weight = input.bone_weights[influence_index];
            if (bone_weight <= 0.0f)
            {
                continue;
            }

            uint bone_index = input.bone_indices[influence_index];
            if (bone_index >= instance_data.bone_count)
            {
                continue;
            }

            float4x4 bone_matrix = g_scene_bones[instance_data.bone_offset + bone_index];
            world_position_xyz += mul(float4(input.position, 1.0f), bone_matrix).xyz * bone_weight;
            world_normal += mul(input.normal, (float3x3)bone_matrix) * bone_weight;
        }
    }
    else
    {
        float4x4 model = g_scene_transforms[instance_data.transform_index];
        float4 static_world_position = mul(float4(input.position, 1.0f), model);
        world_position_xyz = static_world_position.xyz;
        world_normal = mul(input.normal, (float3x3)model);
    }

    float4 world_position = float4(world_position_xyz, 1.0f);
    output.curr_clip_position = mul(world_position, g_view_projection);
    output.position = output.curr_clip_position;
    output.normal = normalize(world_normal);
    output.world_position = world_position_xyz;
    output.texcoord = input.texcoord;
    output.lightmap_texcoord = input.lightmap_texcoord;
    output.material_index = instance_data.material_index;

    if (has_skinned_motion)
    {
        output.prev_clip_position = output.curr_clip_position;
    }
    else
    {
        float4x4 prev_model = g_scene_prev_transforms[instance_data.transform_index];
        float4 prev_world_position = mul(float4(input.position, 1.0f), prev_model);
        output.prev_clip_position = mul(prev_world_position, g_prev_view_projection);
    }

    float3 vertex_color = float3(1.0f, 1.0f, 1.0f);
    if (instance_data.vertex_color_offset != 0xFFFFFFFFu)
    {
        vertex_color = g_scene_vertex_colors[instance_data.vertex_color_offset + vertex_id].rgb;
    }
    else if (instance_data.ambient_cube_offset != 0xFFFFFFFFu)
    {
        vertex_color = VertexShaderAmbientLight(output.normal, instance_data.ambient_cube_offset);
    }

    output.color = vertex_color * instance_data.color.rgb;
    return output;
}
