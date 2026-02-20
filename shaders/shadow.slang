#include "common.slang"

struct OutVertex
{
    float4 position : SV_POSITION;
    float3 world_pos : POSITION;
};

struct PushConstant
{
    uint vertex_buffer_index;
    uint mesh_buffer_index;
    uint mesh_vertex_buffer_index;
    uint mesh_triangle_buffer_index;

    uint transform_index;
    uint meshlet_count;
    uint bounding_offset;
    uint cascade_index;
};
ConstantBuffer<PushConstant> PushConstants : register(b0);

[outputtopology("triangle")]
[numthreads(128, 1, 1)]
[shader("mesh")]
void mesh_main(uint gtid: SV_GroupThreadID,
               uint gid: SV_GroupID,
               OutputVertices<OutVertex, 64> verts,
               OutputIndices<uint3, 124> triangles)
{
    var vertex_buffer = DescriptorHandle<StructuredBuffer<Vertex>>(PushConstants.vertex_buffer_index);
    var meshlet_buffer = DescriptorHandle<StructuredBuffer<Meshlet>>(PushConstants.mesh_buffer_index);
    var mesh_vertex_buffer = DescriptorHandle<StructuredBuffer<uint>>(PushConstants.mesh_vertex_buffer_index);
    var mesh_triangle_buffer = DescriptorHandle<StructuredBuffer<uint>>(PushConstants.mesh_triangle_buffer_index);
    var transform_buffer = DescriptorHandle<StructuredBuffer<float4x4>>(GlobalConstants.transform_buffer_index);

    Meshlet meshlet = meshlet_buffer[gid];
    SetMeshOutputCounts(meshlet.vertex_count, meshlet.triangle_count);

    if (gtid < meshlet.triangle_count)
    {
        uint packed = mesh_triangle_buffer[meshlet.triangle_offset + gtid];
        uint idx0 = (packed >> 0) & 0xFF;
        uint idx1 = (packed >> 8) & 0xFF;
        uint idx2 = (packed >> 16) & 0xFF;
        triangles[gtid] = uint3(idx0, idx1, idx2);
    }

    if (gtid < meshlet.vertex_count)
    {
        var transform = transform_buffer[PushConstants.transform_index];
        uint vertex_index = meshlet.vertex_offset + gtid;
        vertex_index = mesh_vertex_buffer[vertex_index];
        Vertex vertex = vertex_buffer[vertex_index];
        float4 world_pos = mul(transform, float4(vertex.position, 1.0));
        verts[gtid].position = mul(GlobalConstants.sun_view_proj, world_pos);
        verts[gtid].world_pos = world_pos.xyz;
    }
}

[shader("pixel")]
void pixel_main() {}
