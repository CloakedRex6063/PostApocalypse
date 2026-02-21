struct PointLight
{
    float3 position;
    float intensity;
    float3 color;
    uint range;
};

struct DirectionalLight
{
    float3 direction;
    float intensity;
    float3 color;
    uint cast_shadows;
};

static const float PI = 3.14159265359;

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = saturate(dot(N, H));
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return a2 / max(denom, 0.000001);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float denom = NdotV * (1.0 - k) + k;
    return NdotV / denom;
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = saturate(dot(N, V));
    float NdotL = saturate(dot(N, L));
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float3 ApplyLight(float3 n, float3 v, float3 l, float3 f0, float3 albedo, float3 radiance, float roughness, float metallic)
{
    float3 h = normalize(v + l);
    float NdotL = saturate(dot(n, l));
    if (NdotL <= 0.0)
        return 0.0;

    float ndf = DistributionGGX(n, h, roughness);
    float g = GeometrySmith(n, v, l, roughness);
    float3 f = FresnelSchlick(saturate(dot(h, v)), f0);

    float3 numerator = ndf * g * f;
    float denom = 4.0 * saturate(dot(n, v)) * saturate(dot(n, l));
    float3 specular = numerator / max(denom, 0.0001);

    float3 kS = f;
    float3 kD = (1.0 - kS) * (1.0 - metallic);

    float3 diffuse = kD * albedo / PI;
    return (diffuse + specular) * NdotL * radiance;
}

float2 EnvBRDFApprox(float n_dot_v, float roughness)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * n_dot_v)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}

float3 FresnelSchlickRoughness(float n_dot_v, float3 f0, float roughness)
{
    return f0 + (max(float3(1.0 - roughness), f0) - f0) * pow(1.0 - n_dot_v, 5.0);
}

float Fd_Lambert()
{
    return 1.0 / PI;
}

// From https://google.github.io/filament/Filament.md.html#lighting/imagebasedlights

const static float3 sh[9] = float3[9](float3(0.368583410978317, 0.369707107543945, 0.382888227701187),     // L00
                                      float3(0.257704228162766, 0.268975198268890, 0.356777757406235),     // L1-1
                                      float3(0.293762534856796, 0.238645166158676, 0.196910589933395),     // L10
                                      float3(-0.286828815937042, -0.255836755037308, -0.240301072597504),  // L11
                                      float3(-0.165518984198570, -0.149610176682472, -0.146310895681381),  // L2-2
                                      float3(0.187659606337547, 0.158899247646332, 0.139614671468735),     // L2-1
                                      float3(0.023983489722013, 0.016394825652242, 0.006685240194201),     // L20
                                      float3(-0.230607286095619, -0.182966008782387, -0.142911925911903),  // L21
                                      float3(0.026930458843708, 0.018404675647616, -0.000134044690640)     // L22
);

float3 CalculateIrradiance(float3 n)
{
    return max(sh[0] * 0.282095 + sh[1] * 0.488603 * n.y + sh[2] * 0.488603 * n.z + sh[3] * 0.488603 * n.x +
                   sh[4] * 1.092548 * n.x * n.y + sh[5] * 1.092548 * n.y * n.z + sh[6] * 0.315392 * (3.0 * n.z * n.z - 1.0) +
                   sh[7] * 1.092548 * n.x * n.z + sh[8] * 0.546274 * (n.x * n.x - n.y * n.y),
               float3(0.0));
}

float3 CalculateIBL(float3 n, float3 v, float3 albedo, float metallic, float roughness, SamplerCube ibl_texture)
{
    float n_dot_v = max(dot(n, v), 0.0);
    float3 f0 = lerp(float3(0.04), albedo, metallic);
    float3 f = FresnelSchlickRoughness(n_dot_v, f0, roughness);
    float3 f90 = saturate(50.0 * f0);

    float max_mip_level = 6.0;

    float3 r = reflect(-v, n);
    float lod = roughness * max_mip_level;

    float3 indirect_specular = ibl_texture.SampleLevel(r, lod).rgb;
    float horizon = min(1.0 + dot(r, n), 1.0);
    indirect_specular *= horizon * horizon;
    float2 brdf = EnvBRDFApprox(n_dot_v, roughness);
    float3 specular = (f0 * brdf.x + f90 * brdf.y);

    float3 indirect_diffuse = max(CalculateIrradiance(n), 0.0) * Fd_Lambert();

    return albedo * indirect_diffuse + indirect_specular * specular;
}

float CalculateShadow(Sampler2DShadow shadow_texture, uint2 size, float4 light_space_pos, float3 normal, float3 light_dir)
{
    float2 texel_size = float2(1.0 / size.x, 1.0 / size.y);
    float3 proj_coords = light_space_pos.xyz / light_space_pos.w;
    proj_coords.xy = proj_coords.xy * 0.5 + 0.5;
    proj_coords.y = 1.0 - proj_coords.y;

    if (proj_coords.z > 1.0)
        return 1.0;

    float depth = proj_coords.z;

    float shadow = 0.0;
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            shadow += shadow_texture.SampleCmpLevelZero(proj_coords.xy + float2(x, y) * texel_size, depth);
        }
    }

    return lerp(0, 1.0, shadow / 9.0);
}
