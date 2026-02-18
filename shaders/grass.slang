#include "cull.slang"
#include "common.slang"

struct GrassPatch
{
    float3 position;
    float height;
    float2 padding;
    float width = 0.2f;
    float radius = 0.5;
};

struct PushConstant
{
    float wind_speed;
    float wind_strength;
    uint apply_view_space_thicken;
    float lod_distance;

    uint grass_count;
    float time;
};
ConstantBuffer<PushConstant> PushConstants : register(b0);

static const uint GROUP_SIZE = 128;
static const int GRASS_VERT_COUNT = 256;
static const int GRASS_PRIM_COUNT = 192;
static const float PI = 3.14159265359;

float3 Bezier(float3 p0, float3 p1, float3 p2, float t)
{
    float3 a = lerp(p0, p1, t);
    float3 b = lerp(p1, p2, t);
    return lerp(a, b, t);
}

float3 BezierDerivative(float3 p0, float3 p1, float3 p2, float t)
{
    return 2. * (1. - t) * (p1 - p0) + 2. * t * (p2 - p1);
}

float Hash(uint seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return float(seed) / 4294967296.0;
}

float3 Hash3(uint seed)
{
    uint h1 = seed * 73856093u;
    uint h2 = seed * 19349663u;
    uint h3 = seed * 83492791u;

    h1 = (h1 ^ 61u) ^ (h1 >> 16u);
    h2 = (h2 ^ 61u) ^ (h2 >> 16u);
    h3 = (h3 ^ 61u) ^ (h3 >> 16u);

    h1 *= 9u;
    h2 *= 9u;
    h3 *= 9u;

    return float3(h1, h2, h3) * (1.0 / 4294967296.0);
}

float Hash(float3 p)
{
    uint3 q = uint3(int3(p * 1000.0));
    uint n = q.x * 374761393u + q.y * 668265263u + q.z * 1274126177u;
    return Hash(n);
}

float2 RandomPointInCircle(uint seed, float radius)
{
    float random1 = Hash(seed);
    float random2 = Hash(seed + 1u);

    float r = sqrt(random1) * radius;
    float theta = random2 * 2.0 * PI;

    return float2(r * cos(theta), r * sin(theta));
}

float2 RandomPointInCircle(float3 position, float radius)
{
    uint seed = uint(Hash(position));
    return RandomPointInCircle(seed, radius);
}

float3 GetPatchAmbientMotion(float3 center, float time)
{
    float3 offset = float3(0, 0, 0);
    offset.x = (2 * sin(center.x + center.y + center.z + time)) + 1;
    offset.z = sin(2 * (center.x + center.y + center.z + time)) + 0.5;
    return offset;
}

float EaseIn(float t, int power)
{
    return pow(t, float(power));
}

float EaseOut(float x, float power)
{
    return 1.0 - pow(1.0 - x, power);
}

float Noise2D(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);

    f = f * f * (3.0 - 2.0 * f);

    float a = Hash(uint(i.x) + uint(i.y) * 57u);
    float b = Hash(uint(i.x + 1.0) + uint(i.y) * 57u);
    float c = Hash(uint(i.x) + uint(i.y + 1.0) * 57u);
    float d = Hash(uint(i.x + 1.0) + uint(i.y + 1.0) * 57u);

    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float Remap(float value, float from_min, float from_max, float to_min, float to_max)
{
    return to_min + (value - from_min) * (to_max - to_min) / (from_max - from_min);
}

float3 ApplyWind(float3 position)
{
    float wind_dir = Noise2D(position.xz * 0.05 + 0.05 * PushConstants.time);
    wind_dir = Remap(wind_dir, 0.0, 1.0, 0.0, 2.0 * PI);
    float wind_noise_sample = Noise2D(position.xz * 0.25 + PushConstants.time * PushConstants.wind_speed);
    float wind_lean_angle = EaseIn(wind_noise_sample, 2) * 1.0;
    float wind_lean_amount = EaseIn(sin(wind_lean_angle), 2) * 1.25 * PushConstants.wind_strength;
    float3 wind_direction = float3(cos(wind_dir), 0, sin(wind_dir));
    return wind_direction * wind_lean_amount;
}

void MakePersistentLength(in float3 v0, inout float3 v1, inout float3 v2, in float height)
{
    float3 v01 = v1 - v0;
    float3 v12 = v2 - v1;
    float lv01 = length(v01);
    float lv12 = length(v12);

    float L1 = lv01 + lv12;
    float L0 = length(v2 - v0);
    float L = (2.0f * L0 + L1) / 3.0f;

    float ldiff = height / L;
    v01 = v01 * ldiff;
    v12 = v12 * ldiff;
    v1 = v0 + v01;
    v2 = v1 + v12;
}

struct Payload
{
    uint patch_id[32];
};

struct GrassVertex
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
    float3 world_pos : POSITION;
    float density : DENSITY;
};

groupshared Payload s_Payload;

[NumThreads(32, 1, 1)]
[shader("amplification")]
void ampl_main(uint gtid: SV_GroupThreadID, uint gid: SV_GroupID)
{
    uint base_patch = gid * 32;
    uint patch_id = base_patch + gtid;

    bool visible = false;

    if (patch_id < PushConstants.grass_count)
    {
        var grass_buffer = DescriptorHandle<StructuredBuffer<GrassPatch>>(GlobalConstants.grass_buffer_index);
        GrassPatch grass_patch = grass_buffer[patch_id];

        var frustum_buffer = DescriptorHandle<StructuredBuffer<Frustum>>(GlobalConstants.frustum_buffer_index);
        visible =
            IsVisibleAfterFrustumCull(frustum_buffer[0], grass_patch.radius, grass_patch.position, GlobalConstants.cam_pos);
    }

    if (visible)
    {
        uint index = WavePrefixCountBits(visible);
        s_Payload.patch_id[index] = patch_id;
    }
    uint visible_count = WaveActiveCountBits(visible);
    DispatchMesh(visible_count, 1, 1, s_Payload);
}

groupshared float4 blade_randoms[21];

[outputtopology("triangle")]
[numthreads(GROUP_SIZE, 1, 1)]
[shader("mesh")]
void mesh_main(uint gtid: SV_GroupThreadID,
               uint gid: SV_GroupID,
               in payload Payload payload,
               OutputVertices<GrassVertex, 252> verts,
               OutputIndices<uint3, 210> triangles)
{
    uint patch_id = payload.patch_id[gid];

    const uint vertices_per_blade_edge = 6;
    const uint blades_per_patch = GROUP_SIZE / vertices_per_blade_edge;
    const uint triangles_per_blade = 10;
    const float grass_leaning = 0.7;
    const float screen_space_thickness = 0.002;

    uint grass_id = gtid / vertices_per_blade_edge;
    uint local_id = gtid % vertices_per_blade_edge;

    var grass_buffer = DescriptorHandle<StructuredBuffer<GrassPatch>>(GlobalConstants.grass_buffer_index);
    GrassPatch grass_patch = grass_buffer[patch_id];

    float distance_to_camera = distance(GlobalConstants.cam_pos, grass_patch.position);
    float blade_count_f = lerp(float(blades_per_patch), 2.0, saturate(distance_to_camera / PushConstants.lod_distance));
    uint blade_count = uint(ceil(blade_count_f));

    uint actual_verts = blade_count * vertices_per_blade_edge * 2;
    uint actual_tris = blade_count * triangles_per_blade;

    SetMeshOutputCounts(actual_verts, actual_tris);

    if (grass_id >= blade_count)
        return;

    if (local_id == 0)
    {
        uint seed_height = grass_id * 73856093u ^ patch_id * 19349663u;
        uint seed_position = grass_id * 83492791u ^ patch_id * 50331653u;
        uint seed_rotation = grass_id * 19349669u ^ patch_id * 73856093u;

        float2 rand_position = RandomPointInCircle(seed_position, grass_patch.radius);
        float rand_height = Hash(seed_height);
        float rand_rotation = Hash(seed_rotation) * 2.0 * PI;

        blade_randoms[grass_id] = float4(rand_position, rand_height, rand_rotation);
    }

    GroupMemoryBarrierWithGroupSync();

    float4 randoms = blade_randoms[grass_id];
    float2 rand_position = randoms.xy;
    float rand_height = randoms.z;
    float rotation = randoms.w;

    float height = grass_patch.height * (1.0 + 0.5 * rand_height);
    float3 position = grass_patch.position + float3(rand_position.x, 0, rand_position.y);

    float3 up = float3(0, 1, 0);
    float3 base_right = float3(1, 0, 0);
    float3 blade_direction = float3(cos(rotation), 0, sin(rotation));

    float3 wind_direction = ApplyWind(position);

    float3 p0 = position;
    float3 p1 = p0 + float3(0, height * 0.5, 0);
    float base_lean = 0.3 + 0.4 * rand_height;
    float3 p2 = p0 + float3(0, height, 0) + blade_direction * height * base_lean + wind_direction;
    MakePersistentLength(p0, p1, p2, height);

    float t = local_id / float(vertices_per_blade_edge - 1);

    float3 tangent = normalize(BezierDerivative(p0, p1, p2, t));

    float3 vertex_position = Bezier(p0, p1, p2, t);

    float3 side_vec = normalize(float3(blade_direction.y, 0, -blade_direction.x));
    float3 normal = cross(side_vec, tangent);

    float width = grass_patch.width * (1.0 - t);

    width *= blades_per_patch / blade_count_f;

    if (grass_id == (blade_count - 1))
    {
        width *= frac(blade_count_f);
    }

    float3 left_vector = vertex_position - blade_direction * width * 0.5;
    float3 right_vector = vertex_position + blade_direction * width * 0.5;

    float3 view_dir = normalize(GlobalConstants.cam_pos - vertex_position);
    float view_dot_normal = saturate(dot(normal.xz, view_dir.xz));
    float view_space_thicken_factor = EaseOut(1.0 - view_dot_normal, 4.0);
    float3 adjustment_direction = normalize(cross(view_dir, normal));

    if (bool(PushConstants.apply_view_space_thicken))
    {
        left_vector += view_space_thicken_factor * adjustment_direction * width;
        right_vector += view_space_thicken_factor * adjustment_direction * width;
    }

    uint base_vertex_index = grass_id * vertices_per_blade_edge * 2;

    float density = float(blade_count) / float(blades_per_patch);
    verts[base_vertex_index + local_id * 2].position = mul(GlobalConstants.view_proj, float4(left_vector, 1));
    verts[base_vertex_index + local_id * 2].normal = normal;
    verts[base_vertex_index + local_id * 2].uv = float2(0.0, t);
    verts[base_vertex_index + local_id * 2].world_pos = left_vector;
    verts[base_vertex_index + local_id * 2].density = density;

    verts[base_vertex_index + local_id * 2 + 1].position = mul(GlobalConstants.view_proj, float4(right_vector, 1));
    verts[base_vertex_index + local_id * 2 + 1].normal = normal;
    verts[base_vertex_index + local_id * 2 + 1].uv = float2(1.0, t);
    verts[base_vertex_index + local_id * 2 + 1].world_pos = right_vector;
    verts[base_vertex_index + local_id * 2 + 1].density = density;

    if (local_id < (vertices_per_blade_edge - 1))
    {
        uint base_tri_index = grass_id * triangles_per_blade;
        uint v0 = base_vertex_index + local_id * 2;

        triangles[base_tri_index + local_id * 2] = uint3(v0, v0 + 1, v0 + 2);
        triangles[base_tri_index + local_id * 2 + 1] = uint3(v0 + 1, v0 + 3, v0 + 2);
    }
}

[shader("pixel")]
float4 pixel_main(GrassVertex input) : SV_TARGET
{
    float3 normal = input.normal;

    float3 base_color = float3(0.05, 0.2, 0.01);
    float3 tip_color = float3(0.5, 0.5, 0.1);
    float3 grass_color = lerp(base_color, tip_color, EaseIn(input.uv.y, 4));

    float selfshadow = pow(input.uv.y, 1.5);

    float ao_for_density = lerp(1.0, 0.25, input.density);
    float ao = lerp(ao_for_density, 1.0, EaseIn(input.uv.y, 2));

    float occlusion = ao * selfshadow;

    float3 lo = grass_color + 0.3 * occlusion;

    return float4(lo, 1.0);
}
