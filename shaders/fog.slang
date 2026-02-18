#include "common.slang"

struct OutVertex
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD;
};

[outputtopology("triangle")]
[numthreads(3, 1, 1)]
[shader("mesh")]
void mesh_main(uint gtid: SV_GroupThreadID,
               uint gid: SV_GroupID,
               OutputVertices<OutVertex, 3> verts,
               OutputIndices<uint3, 1> triangles)
{
    SetMeshOutputCounts(3, 1);

    if (gtid == 0)
        triangles[0] = uint3(0, 2, 1);

    float2 uv = float2((gtid << 1) & 2, gtid & 2);
    verts[gtid].position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    verts[gtid].uv = uv;
}

struct PushConstant
{
    float4x4 inv_view_proj;
    uint scene_texture_index;
    uint depth_texture_index;
    float fog_density;
    float fog_max_distance;

    float3 fog_color;
    uint ray_march_steps;
}
ConstantBuffer<PushConstant> PushConstants : register(b0);

float3 WorldPosFromDepth(float depth, float2 uv)
{
    float4 clip_space = float4((uv * 2.0 - 1.0) * float2(1, -1), depth, 1.0);
    float4 view_space = mul(PushConstants.inv_view_proj, clip_space);
    return view_space.xyz / view_space.w;
}

float GetFogDensity(float3 world_pos)
{
    float height = world_pos.y;
    float height_density = exp(-max(0.0, height - 5.0) * 0.08);
    return height_density * PushConstants.fog_density;
}

SamplerState point_samp : register(s0);
SamplerState samp : register(s1);

[shader("pixel")]
float4 pixel_main(OutVertex input) : SV_TARGET
{
    var depth_texture = DescriptorHandle<Texture2D<float>>(PushConstants.depth_texture_index);
    float depth = depth_texture.Sample(point_samp, input.uv);

    var scene_texture = DescriptorHandle<Texture2D>(PushConstants.scene_texture_index);
    float3 scene_color = scene_texture.Sample(samp, input.uv).rgb;

    float3 world_pos = WorldPosFromDepth(depth, input.uv);

    float3 ray_dir = normalize(world_pos - GlobalConstants.cam_pos);
    float ray_len = min(length(world_pos - GlobalConstants.cam_pos), PushConstants.fog_max_distance);

    float step_size = ray_len / float(PushConstants.ray_march_steps);
    float3 ray_step = ray_dir * step_size;
    float3 current_pos = GlobalConstants.cam_pos;

    float fog_accumulation = 0.0;
    for (uint i = 0; i < PushConstants.ray_march_steps; i++)
    {
        current_pos += ray_step;
        float density = GetFogDensity(current_pos);
        fog_accumulation += density * step_size;

        if (fog_accumulation >= 1.0)
        {
            fog_accumulation = 1.0;
            break;
        }
    }

    float3 final_color = lerp(scene_color, PushConstants.fog_color, fog_accumulation);

    return float4(final_color, 1.0);
}
