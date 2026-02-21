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

const static int MAX_KERNEL_SIZE = 20;
const static float INV_KERNEL_SIZE = 1.0 / float(MAX_KERNEL_SIZE);
const static float2 HALF_2 = float2(0.5, 0.5);

float3 GetViewPosition(float depth, float2 tex_coords)
{
    float4 ndc_space = float4(tex_coords.x * 2.0 - 1.0, tex_coords.y * 2.0 - 1.0, depth, 1.0);
    float4 vs_space = mul(GlobalConstants.inv_proj, ndc_space);
    return vs_space.xyz / vs_space.w;
}

struct PushConstant
{
    uint depth_texture_index;
    uint sampler_index;
    uint noise_texture_index;

    uint kernel_buffer_index;
    float sample_rad;
    float2 noise_scale;
}
ConstantBuffer<PushConstant> PushConstants : register(b0);

struct Kernel
{
    float3 kernel;
    float padding;
}

[shader("pixel")]
float pixel_main(OutVertex input) : SV_TARGET
{
    var depth_sampler = DescriptorHandle<Sampler2D<float>>(uint2(PushConstants.depth_texture_index, PushConstants.sampler_index));
    float depth = depth_sampler.Sample(input.uv);
    float3 view_pos = GetViewPosition(depth, input.uv);
    float3 view_normal = cross(ddx_fine(view_pos), ddy_fine(view_pos)) * -1.0;

    var noise_sampler = DescriptorHandle<Sampler2D>(uint2(PushConstants.depth_texture_index, PushConstants.sampler_index));
    float3 random_vec = noise_sampler.Sample(input.uv * PushConstants.noise_scale).xyz;

    float3 tangent = normalize(random_vec - view_normal * dot(random_vec, view_normal));
    float3 bitangent = cross(view_normal, tangent);
    float3x3 TBN = float3x3(tangent, bitangent, view_normal);

    var kernel_buffer = DescriptorHandle<StructuredBuffer<Kernel>>(PushConstants.kernel_buffer_index);

    float occlusion_factor = 0.0;
    for (int i = 0; i < MAX_KERNEL_SIZE; i++)
    {
        float3 sample_pos = mul(TBN, kernel_buffer[i].kernel);
        sample_pos = view_pos + sample_pos * PushConstants.sample_rad;

        float4 offset = float4(sample_pos, 1.0);
        offset = mul(GlobalConstants.proj, offset);
        offset.xy /= offset.w;
        offset.xy = offset.xy * HALF_2 + HALF_2;

        float geom_depth = GetViewPosition(depth, offset.xy).z;
        float range_check = smoothstep(0.0, 1.0, PushConstants.sample_rad / abs(view_pos.z - geom_depth));

        occlusion_factor += float(geom_depth >= sample_pos.z + 0.0001) * range_check;
    }

    occlusion_factor *= INV_KERNEL_SIZE;
    float visibility_factor = 1.0 - occlusion_factor;

    return visibility_factor;
}
