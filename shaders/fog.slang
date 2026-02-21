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
    uint scene_texture_index;
    uint depth_texture_index;
    uint bilinear_sampler_index;
    uint point_sampler_index;
    uint shadow_sampler_index;
}
ConstantBuffer<PushConstant> PushConstants : register(b0);

float3 WorldPosFromDepth(float4x4 inv_view_proj, float depth, float2 uv)
{
    float4 clip_space = float4((uv * 2.0 - 1.0) * float2(1, -1), depth, 1.0);
    float4 view_space = mul(inv_view_proj, clip_space);
    return view_space.xyz / view_space.w;
}

float GetFogDensity(float3 world_pos, float fog_density)
{
    float height = world_pos.y;
    float height_density = exp(-max(0.0, height - 5.0) * 0.08);
    return height_density * fog_density;
}

float PhaseHG(float cosTheta, float g)
{
    float g2 = g * g;
    return (1.0 - g2) / pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5) * (1.0 / (4.0 * 3.14159265));
}

float GetLightVisibility(float4x4 sun_view_proj, float3 world_pos, Sampler2DShadow shadow_texture)
{
    float4 light_space_pos = mul(sun_view_proj, float4(world_pos, 1.0));
    float3 proj_coords = light_space_pos.xyz / light_space_pos.w;
    proj_coords.xy = proj_coords.xy * 0.5 + 0.5;
    proj_coords.y = 1.0 - proj_coords.y;

    if (proj_coords.z > 1.0)
        return 1.0;

    float depth = proj_coords.z;

    float shadow = shadow_texture.SampleCmpLevelZero(proj_coords.xy, depth);

    return shadow;
}

[shader("pixel")]
float4 pixel_main(OutVertex input) : SV_TARGET
{
    var depth_texture =
        DescriptorHandle<Sampler2D<float>>(uint2(PushConstants.depth_texture_index, PushConstants.point_sampler_index));
    float depth = depth_texture.Sample(input.uv);

    var scene_texture =
        DescriptorHandle<Sampler2D>(uint2(PushConstants.scene_texture_index, PushConstants.bilinear_sampler_index));
    float3 scene_color = scene_texture.Sample(input.uv).rgb;

    float3 world_pos = WorldPosFromDepth(GlobalConstants.inv_view_proj, depth, input.uv);

    float3 ray_dir = normalize(world_pos - GlobalConstants.cam_pos);
    float ray_len = min(length(world_pos - GlobalConstants.cam_pos), GlobalConstants.fog_max_distance);

    float step_size = ray_len / float(GlobalConstants.ray_march_steps);
    float3 ray_step = ray_dir * step_size;
    float3 current_pos = GlobalConstants.cam_pos;

    float3 transmittance = float3(1, 1, 1);
    float3 scattering = float3(0, 0, 0);
    var sun = DescriptorHandle<StructuredBuffer<DirectionalLight>>(GlobalConstants.dir_light_buffer_index)[0];
    float3 sun_color = sun.color;
    float3 sun_dir = -normalize(sun.direction);
    float sun_intensity = sun.intensity;

    float g = GlobalConstants.scattering_factor;
    float cosTheta = dot(ray_dir, sun_dir);
    float phase = PhaseHG(cosTheta, g);

    var shadow_texture = DescriptorHandle<Texture2D<float>>(GlobalConstants.shadow_map_index);
    var shadow_sampler =
        DescriptorHandle<Sampler2DShadow>(uint2(GlobalConstants.shadow_map_index, PushConstants.shadow_sampler_index));
    uint width;
    uint height;
    shadow_texture.GetDimensions(width, height);

    for (uint i = 0; i < GlobalConstants.ray_march_steps; i++)
    {
        current_pos += ray_step;
        float density = GetFogDensity(current_pos, GlobalConstants.fog_density);
        float sigma_s = density * GlobalConstants.scattering_coefficient;
        float3 sigma_a = density * GlobalConstants.absorption_coefficient * GlobalConstants.absorption_color;
        float3 sigma_t = sigma_s + sigma_a;
        float light_vis = GetLightVisibility(GlobalConstants.sun_view_proj, current_pos, shadow_sampler);

        float3 sun_scatter = sigma_s * light_vis * sun_color * sun_intensity * phase;
        float3 ambient_scatter = sigma_s * GlobalConstants.fog_color;
        float3 total_light = sun_scatter + ambient_scatter;
        scattering += transmittance * total_light * step_size;

        transmittance *= exp(-sigma_t * step_size);

        if (max(transmittance.x, max(transmittance.y, transmittance.z)) < 0.01)
            break;
    }

    float3 final_color = scene_color * transmittance + scattering;

    return float4(final_color, 1.0);
}
