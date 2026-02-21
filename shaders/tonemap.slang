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

// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
float3 Aces(float3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

struct PushConstant
{
    uint source_index;
    uint sampler_index;
    float exposure;
}
ConstantBuffer<PushConstant> PushConstants : register(b0);

[shader("pixel")]
float4 pixel_main(OutVertex input) : SV_TARGET
{
    var sampler = DescriptorHandle<Sampler2D>(uint2(PushConstants.source_index, PushConstants.sampler_index));
    float3 hdr = sampler.Sample(input.uv).xyz;
    hdr *= PushConstants.exposure;
    float3 color = Aces(hdr);
    color = pow(color, float3(1.0 / 2.2));
    return float4(color, 1.0);
}
