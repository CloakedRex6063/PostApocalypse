struct Frustum
{
    float4 planes[6];
};

struct CullData
{
    float3 center;
    float radius;
    float3 cone_apex;
    uint32_t cone_packed;
};

struct TaskPayload
{
    uint meshlet_index[32];
};

groupshared TaskPayload s_Payload;

float3 GetScale(float4x4 m)
{
    return float3(length(m[0].xyz), length(m[1].xyz), length(m[2].xyz));
}

bool IsVisibleAfterFrustumCull(Frustum frustum, CullData cull_data, float4x4 transform, float3 camera_position)
{
    float4 center = mul(transform, float4(cull_data.center, 1));
    var scale = GetScale(transform);
    var max_scale = max(scale.x, max(scale.y, scale.z));
    float radius = cull_data.radius * max_scale;
    for (int i = 0; i < 6; ++i)
    {
        if (dot(center, frustum.planes[i]) < -radius)
        {
            return false;
        }
    }

    int3 axis_s8 = int3(int(cull_data.cone_packed << 24) >> 24,
                        int(cull_data.cone_packed << 16) >> 24,
                        int(cull_data.cone_packed << 8) >> 24);
    int cutoff_s8 = int(cull_data.cone_packed) >> 24;

    float3 cone_axis = float3(axis_s8) / 127.0;
    float cone_cutoff = float(cutoff_s8) / 127.0;

    if (cone_cutoff > 1.0)
        return true;

    float3 apex_world = mul(transform, float4(cull_data.cone_apex, 1.0)).xyz;
    float3 axis_world = normalize(mul(transform, float4(cone_axis, 0.0))).xyz;
    float3 view = normalize(camera_position - apex_world);

    if (dot(view, -axis_world) > cone_cutoff)
        return false;

    return true;
}