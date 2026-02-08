cbuffer GlobalConstantBuffer : register(b1)
{
    float4x4 view_proj;
    float4x4 view;
    float4x4 proj;

    float3 cam_pos;
    uint cubemap_index;

    uint transform_buffer_index;
    uint material_buffer_index;
    uint point_light_buffer_index;
    uint dir_light_buffer_index;
    uint point_light_count;
    uint dir_light_count;
} GlobalConstants;

struct Meshlet
{
    uint vertex_offset;
    uint triangle_offset;
    uint vertex_count;
    uint triangle_count;
};

struct Material
{
    float4 albedo;

    float3 emissive;
    int albedo_index;

    int emissive_index;
    int metal_rough_index;
    float metallic;
    float roughness;

    int normal_index;
    int occlusion_index;
    float alpha_cutoff;
    uint alpha_mode;
};

struct PushConstant
{
    uint vertex_buffer_index;
    uint mesh_buffer_index;
    uint mesh_vertex_buffer_index;
    uint mesh_triangle_buffer_index;
    uint material_index;
    uint transform_index;
};

ConstantBuffer<PushConstant> PushConstants : register(b0);
