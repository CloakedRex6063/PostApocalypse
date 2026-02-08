#include "common.slang"

struct Vertex
{
    float4 position : SV_POSITION;
    float3 tex_coords : TEXCOORD0;
};

static const float3 skybox_vertices[36] = { // Back face
                                            float3(-1.0, -1.0, -1.0),
                                            float3(1.0, 1.0, -1.0),
                                            float3(1.0, -1.0, -1.0),
                                            float3(1.0, 1.0, -1.0),
                                            float3(-1.0, -1.0, -1.0),
                                            float3(-1.0, 1.0, -1.0),

                                            // Front face
                                            float3(-1.0, -1.0, 1.0),
                                            float3(1.0, -1.0, 1.0),
                                            float3(1.0, 1.0, 1.0),
                                            float3(1.0, 1.0, 1.0),
                                            float3(-1.0, 1.0, 1.0),
                                            float3(-1.0, -1.0, 1.0),

                                            // Left face
                                            float3(-1.0, 1.0, 1.0),
                                            float3(-1.0, 1.0, -1.0),
                                            float3(-1.0, -1.0, -1.0),
                                            float3(-1.0, -1.0, -1.0),
                                            float3(-1.0, -1.0, 1.0),
                                            float3(-1.0, 1.0, 1.0),

                                            // Right face
                                            float3(1.0, 1.0, 1.0),
                                            float3(1.0, -1.0, -1.0),
                                            float3(1.0, 1.0, -1.0),
                                            float3(1.0, -1.0, -1.0),
                                            float3(1.0, 1.0, 1.0),
                                            float3(1.0, -1.0, 1.0),

                                            // Bottom face
                                            float3(-1.0, -1.0, -1.0),
                                            float3(1.0, -1.0, -1.0),
                                            float3(1.0, -1.0, 1.0),
                                            float3(1.0, -1.0, 1.0),
                                            float3(-1.0, -1.0, 1.0),
                                            float3(-1.0, -1.0, -1.0),

                                            // Top face
                                            float3(-1.0, 1.0, -1.0),
                                            float3(1.0, 1.0, 1.0),
                                            float3(1.0, 1.0, -1.0),
                                            float3(1.0, 1.0, 1.0),
                                            float3(-1.0, 1.0, -1.0),
                                            float3(-1.0, 1.0, 1.0)
};

const static uint MaxVertices = 36;
const static uint MaxPrimitives = 12;

SamplerState samp : register(s0);

[outputtopology("triangle")]
[numthreads(12, 1, 1)]
[shader("mesh")]
void mesh_main(uint gtid: SV_GroupThreadID,
               OutputVertices<Vertex, MaxVertices> verts,
               OutputIndices<uint3, MaxPrimitives> tris)
{
    SetMeshOutputCounts(MaxVertices, MaxPrimitives);

    float3x3 view_rot = float3x3(GlobalConstants.view[0].xyz, GlobalConstants.view[1].xyz, GlobalConstants.view[2].xyz);

    for (uint i = 0; i < 3; i++)
    {
        uint vtx_idx = gtid * 3 + i;
        if (vtx_idx < MaxVertices)
        {
            float3 local_pos = skybox_vertices[vtx_idx];
            float3 rotated_pos = mul(view_rot, local_pos);
            float4 clip_pos = mul(GlobalConstants.proj, float4(rotated_pos, 1.0));

            verts[vtx_idx].position = clip_pos.xyww;
            verts[vtx_idx].tex_coords = local_pos;
        }
    }

    if (gtid < MaxPrimitives)
    {
        uint base_index = gtid * 3;
        tris[gtid] = uint3(base_index, base_index + 1, base_index + 2);
    }
}

[shader("pixel")]
float4 pixel_main(Vertex input) : SV_Target
{
    var skybox_texture = DescriptorHandle<TextureCube>(GlobalConstants.cubemap_index);
    float3 env_color = skybox_texture.Sample(samp, input.tex_coords).rgb;
    return float4(env_color, 1.0);
}
