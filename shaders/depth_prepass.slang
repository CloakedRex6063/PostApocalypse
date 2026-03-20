#include "common.slang"
#include "cull.slang"

struct OutVertex
{
    float4 position : SV_POSITION;
    float3 world_pos : POSITION;
};

struct PushConstant
{
    uint position_buffer_index;
    uint mesh_buffer_index;
    uint mesh_vertex_buffer_index;
    uint mesh_triangle_buffer_index;

    uint transform_index;
    uint meshlet_count;
    uint bounding_offset;
};
ConstantBuffer<PushConstant> PushConstants : register(b0);

struct TaskPayload
{
    uint meshlet_index[32];
};

groupshared TaskPayload s_Payload;

[numthreads(32, 1, 1)]
[shader("amplification")]
void ampl_main(uint dtid: SV_DispatchThreadID, uint gtid: SV_GroupThreadID, uint gid: SV_GroupID)
{
    bool visible = false;

    if (dtid < PushConstants.meshlet_count)
    {
        var transform_buffer = DescriptorHandle<StructuredBuffer<float4x4>>(GlobalConstants.transform_buffer_index);
        var transform = transform_buffer[PushConstants.transform_index];
        var frustum_buffer = DescriptorHandle<StructuredBuffer<Frustum>>(GlobalConstants.frustum_buffer_index);
        var bounding_buffer = DescriptorHandle<StructuredBuffer<CullData>>(GlobalConstants.bounding_buffer_index);
        var cull_data = bounding_buffer[dtid + PushConstants.bounding_offset];
        visible = IsVisibleAfterFrustumAndConeCull(frustum_buffer[0], cull_data, transform, GlobalConstants.cam_pos);
    }

    if (visible)
    {
        uint index = WavePrefixCountBits(visible);
        s_Payload.meshlet_index[index] = dtid;
    }

    uint visible_count = WaveActiveCountBits(visible);
    DispatchMesh(visible_count, 1, 1, s_Payload);
}

[outputtopology("triangle")]
[numthreads(128, 1, 1)]
[shader("mesh")]
void mesh_main(uint gtid: SV_GroupThreadID,
               uint gid: SV_GroupID,
               in payload TaskPayload payload,
               OutputVertices<OutVertex, 64> verts,
               OutputIndices<uint3, 124> triangles)
{
    var position_buffer = DescriptorHandle<StructuredBuffer<float3>>(PushConstants.position_buffer_index);
    var meshlet_buffer = DescriptorHandle<StructuredBuffer<Meshlet>>(PushConstants.mesh_buffer_index);
    var mesh_vertex_buffer = DescriptorHandle<StructuredBuffer<uint>>(PushConstants.mesh_vertex_buffer_index);
    var mesh_triangle_buffer = DescriptorHandle<StructuredBuffer<uint>>(PushConstants.mesh_triangle_buffer_index);
    var transform_buffer = DescriptorHandle<StructuredBuffer<float4x4>>(GlobalConstants.transform_buffer_index);

    uint meshlet_index = payload.meshlet_index[gid];
    Meshlet meshlet = meshlet_buffer[meshlet_index];
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
        float3 position = position_buffer[vertex_index];
        float4 world_pos = mul(transform, float4(position, 1.0));
        verts[gtid].position = mul(GlobalConstants.view_proj, world_pos);
        verts[gtid].world_pos = world_pos.xyz;
    }
}

[shader("pixel")]
void pixel_main() {}
