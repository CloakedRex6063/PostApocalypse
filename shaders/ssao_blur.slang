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
    uint32_t ssao_texture_index;
    uint32_t sampler_index;
}
ConstantBuffer<PushConstant> PushConstants : register(b0);

[shader("pixel")]
float pixel_main(OutVertex input) : SV_TARGET
{
    var ssao_texture = DescriptorHandle<Texture2D<float>>(PushConstants.ssao_texture_index);
    uint width, height;
    ssao_texture.GetDimensions(width, height);
    float2 texel_size = float2(1.0, 1.0) / float2(width, height);
    var sampler = DescriptorHandle<Sampler2D<float>>(uint2(PushConstants.ssao_texture_index, PushConstants.sampler_index));

    float result = 0.0;

    for(int x = -2; x < 2; x++)
    {
        for(int y = -2; y < 2; y++)
        {
            result += sampler.Sample(input.uv + texel_size * float2(x, y));
        }
    }

    return result / 16.0;
}
