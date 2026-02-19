#include "common.slang"
#include "lights.slang"

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

    uint shadow_map_index;
    float scattering_factor;
    float scattering_coefficient;
    float absorption_coefficient;

    float3 absorption_color;
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

float PhaseHG(float cosTheta, float g)
{
    float g2 = g * g;
    return (1.0 - g2) / pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5) * (1.0 / (4.0 * 3.14159265));
}

float GetLightVisibility(float3 world_pos, Texture2D<float> shadow_texture, SamplerComparisonState shadow_samp)
{
    uint width;
    uint height;
    shadow_texture.GetDimensions(width, height);
    float2 texel_size = float2(1.0 / width, 1.0 / height);
    float4 light_space_pos = mul(GlobalConstants.sun_view_proj, float4(world_pos, 1.0));
    float3 proj_coords = light_space_pos.xyz / light_space_pos.w;
    proj_coords.xy = proj_coords.xy * 0.5 + 0.5;
    proj_coords.y = 1.0 - proj_coords.y;

    if (proj_coords.z > 1.0)
        return 1.0;

    float depth = proj_coords.z;

    float shadow = 0.0;
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            shadow += shadow_texture.SampleCmpLevelZero(shadow_samp, proj_coords.xy + float2(x, y) * texel_size, depth);
        }
    }

    return shadow / 9.0;
}

SamplerState point_samp : register(s0);
SamplerState samp : register(s1);
SamplerComparisonState shadow_samp : register(s2);

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

    float3 transmittance = float3(1, 1, 1);
    float3 scattering = float3(0, 0, 0);
    var shadow_texture = DescriptorHandle<Texture2D<float>>(PushConstants.shadow_map_index);
    var sun = DescriptorHandle<StructuredBuffer<DirectionalLight>>(GlobalConstants.dir_light_buffer_index)[0];
    float3 sun_color = sun.color;
    float3 sun_dir = -normalize(sun.direction);

    float g = PushConstants.scattering_factor;
    float cosTheta = dot(ray_dir, sun_dir);
    float phase = PhaseHG(cosTheta, g);

    for (uint i = 0; i < PushConstants.ray_march_steps; i++)
    {
        current_pos += ray_step;
        float density = GetFogDensity(current_pos);
        float sigma_s = density * PushConstants.scattering_coefficient;
        float3 sigma_a = density * PushConstants.absorption_coefficient * PushConstants.absorption_color;
        float3 sigma_t = sigma_s + sigma_a;
        float light_vis = GetLightVisibility(current_pos, shadow_texture, shadow_samp);

        float3 sun_scatter = sigma_s * light_vis * sun_color * phase;
        float3 ambient_scatter = sigma_s * PushConstants.fog_color;
        float3 total_light = sun_scatter + ambient_scatter;
        scattering += transmittance * total_light * step_size;

        transmittance *= exp(-sigma_t * step_size);

        if (max(transmittance.x, max(transmittance.y, transmittance.z)) < 0.01)
            break;
    }

    float3 final_color = scene_color * transmittance + scattering;

    return float4(final_color, 1.0);
}
