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
    uint scene_texture_index;
    uint bloom_blur_texture_index;
    uint bilinear_sampler_index;
    float exposure;
}
ConstantBuffer<PushConstant> PushConstants : register(b0);

[shader("pixel")]
float4 pixel_main(OutVertex input) : SV_Target
{
    var scene_sampler =
        DescriptorHandle<Sampler2D>(uint2(PushConstants.scene_texture_index, PushConstants.bilinear_sampler_index));
    var bloom_sampler =
        DescriptorHandle<Sampler2D>(uint2(PushConstants.bloom_blur_texture_index, PushConstants.bilinear_sampler_index));

    float3 sampled_color = scene_sampler.Sample(input.uv).xyz + bloom_sampler.Sample(input.uv).xyz;
    float3 result = float3(1.0) - exp(-sampled_color * PushConstants.exposure);
    return float4(result, 1.0);
}
