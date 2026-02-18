cbuffer GlobalConstantBuffer : register(b1)
{
    float4x4 view_proj;
    float4x4 view;
    float4x4 proj;
    float4x4 sun_view_proj;

    float3 cam_pos;
    uint cubemap_index;

    uint transform_buffer_index;
    uint material_buffer_index;
    uint bounding_buffer_index;
    uint frustum_buffer_index;

    uint point_light_buffer_index;
    uint dir_light_buffer_index;
    uint point_light_count;
    uint dir_light_count;

    uint shadow_map_index;
    uint grass_buffer_index;
} GlobalConstants;

struct Meshlet
{
    uint vertex_offset;
    uint triangle_offset;
    uint vertex_count;
    uint triangle_count;
};

struct Vertex
{
    float3 position;
    float uv_x;
    float3 normal;
    float uv_y;
    float4 tangent;
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

struct Cascade
{
    float4x4 view_proj;
    float split_depth;
    float3 padding;
};