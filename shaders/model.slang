#include "common.slang"
#include "lights.slang"
#include "cull.slang"

struct PushConstant
{
    uint vertex_buffer_index;
    uint mesh_buffer_index;
    uint mesh_vertex_buffer_index;
    uint mesh_triangle_buffer_index;

    int material_index;
    uint transform_index;
    uint meshlet_count;
    uint bounding_offset;

    uint ibl_index;
};

ConstantBuffer<PushConstant> PushConstants : register(b0);

struct OutVertex
{
    float4 position : SV_POSITION;
    float3 world_pos : POSITION;
    float4 light_space_pos : POSITION1;
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
};

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
        visible = IsVisibleAfterFrustumCull(frustum_buffer[0], cull_data, transform, GlobalConstants.cam_pos);
    }
    else
    {
        visible = false;
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
    var vertex_buffer = DescriptorHandle<StructuredBuffer<Vertex>>(PushConstants.vertex_buffer_index);
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
        Vertex vertex = vertex_buffer[vertex_index];
        float4 world_pos = mul(transform, float4(vertex.position, 1.0));
        verts[gtid].position = mul(GlobalConstants.view_proj, world_pos);
        verts[gtid].world_pos = world_pos.xyz;
        verts[gtid].light_space_pos = mul(GlobalConstants.sun_view_proj, world_pos);
        verts[gtid].normal = mul(transform, float4(vertex.normal, 1.0)).rgb;
        verts[gtid].uv = float2(vertex.uv_x, vertex.uv_y);
        verts[gtid].tangent = mul(transform, vertex.tangent);
    }
}

SamplerState samp : register(s0);
SamplerComparisonState shadow_samp : register(s1);

[shader("pixel")]
float4 pixel_main(OutVertex input) : SV_TARGET
{
    var material_buffer = DescriptorHandle<StructuredBuffer<Material>>(GlobalConstants.material_buffer_index);
    Material material = material_buffer[PushConstants.material_index];

    float3 albedo = material.albedo.rgb;
    var albedo_texture = DescriptorHandle<Texture2D>(uint(material.albedo_index));
    albedo *= albedo_texture.Sample(samp, input.uv).rgb;

    float metallic = material.metallic;
    float roughness = material.roughness;
    var metal_rough_texture = DescriptorHandle<Texture2D<float4>>(uint(material.metal_rough_index));
    float4 mr = metal_rough_texture.Sample(samp, input.uv);
    roughness *= mr.g;
    metallic *= mr.b;

    float3 emissive = material.emissive;
    var emissive_texture = DescriptorHandle<Texture2D>(uint(material.emissive_index));
    emissive *= emissive_texture.Sample(samp, input.uv).rgb;

    float3 world_pos = input.world_pos;
    float3 v = normalize(GlobalConstants.cam_pos - world_pos);
    float3 f0 = float3(0.04, 0.04, 0.04);
    f0 = lerp(f0, albedo, metallic);

    float3 n = input.normal;
    var normal_texture = DescriptorHandle<Texture2D>(uint(material.normal_index));
    float3 tangent_normal = normal_texture.Sample(samp, input.uv).rgb * 2.0 - 1.0;
    float3 bitangent = cross(input.normal, input.tangent.xyz) * input.tangent.w;
    float3x3 TBN = float3x3(input.tangent.xyz, bitangent, input.normal);
    n = normalize(mul(tangent_normal, TBN));

    float occlusion = 1.0;
    var occlusion_texture = DescriptorHandle<Texture2D>(uint(material.occlusion_index));
    occlusion *= occlusion_texture.Sample(samp, input.uv).r;

    float3 lo = float3(0.0, 0.0, 0.0);
    var point_light_buffer = DescriptorHandle<StructuredBuffer<PointLight>>(GlobalConstants.point_light_buffer_index);
    for (int i = 0; i < GlobalConstants.point_light_count; i++)
    {
        PointLight point_light = point_light_buffer[i];
        float3 l = normalize(point_light.position - world_pos);
        float distance = length(point_light.position - world_pos);
        float attenuation = 1.0 / (distance * distance);
        float3 radiance = point_light.color * point_light.intensity * attenuation;
        lo += ApplyLight(n, v, l, f0, albedo, radiance, roughness, metallic);
    };

    var dir_light_buffer = DescriptorHandle<StructuredBuffer<DirectionalLight>>(GlobalConstants.dir_light_buffer_index);
    for (int i = 0; i < GlobalConstants.dir_light_count; i++)
    {
        DirectionalLight dir_light = dir_light_buffer[i];
        float3 l = normalize(dir_light.direction);
        float3 radiance = dir_light.color * dir_light.intensity;
        lo += ApplyLight(n, v, l, f0, albedo, radiance, roughness, metallic);
    };

    var shadow_texture = DescriptorHandle<Texture2D<float>>(GlobalConstants.shadow_map_index);
    float shadow = CalculateShadow(shadow_texture, shadow_samp, input.light_space_pos, n, dir_light_buffer[0].direction);

    var specular_ibl_texture = DescriptorHandle<TextureCube>(PushConstants.ibl_index);
    float3 ibl = CalculateIBL(n, v, albedo, metallic, roughness, specular_ibl_texture, samp);
    float3 color = ibl + (1.0 - shadow) * lo + emissive;
    color = color / (color + float3(1.0, 1.0, 1.0));
    return float4(color, 1.0);
}
