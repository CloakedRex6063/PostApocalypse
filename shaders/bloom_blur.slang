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
    uint bloom_texture_index;
    uint bilinear_sampler_index;
    uint horizontal;
}
ConstantBuffer<PushConstant> PushConstants : register(b0);

const static float weight[5] = {0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216};

[shader("pixel")]
float4 pixel_main(OutVertex input) : SV_Target
{
    var texture = DescriptorHandle<Texture2D>(PushConstants.bloom_texture_index);
    var sampler = DescriptorHandle<Sampler2D>(uint2(PushConstants.bloom_texture_index, PushConstants.bilinear_sampler_index));
    float3 color = sampler.Sample(input.uv).rgb;

    uint width, height;
    texture.GetDimensions(width, height);
    float2 texel_size = 1.0 / float2(width, height);
    float3 result = color * weight[0];

    if (bool(PushConstants.horizontal))
    {
        [ForceUnroll]
        for (int i = 1; i < 5; i++)
        {
            result += sampler.Sample(input.uv + float2(texel_size.x * i, 0.0)).xyz * weight[i];
            result += sampler.Sample(input.uv + float2(texel_size.x * i, 0.0)).xyz * weight[i];
        }
    }
    else
    {
        [ForceUnroll]
        for (int i = 1; i < 5; i++)
        {
            result += sampler.Sample(input.uv + float2(0.0, texel_size.y * i)).xyz * weight[i];
            result += sampler.Sample(input.uv + float2(0.0, texel_size.y * i)).xyz * weight[i];
        }
    }
    return float4(result, 1.0);
}
