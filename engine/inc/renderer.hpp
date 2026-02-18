#pragma once
#include "camera.hpp"
#include "resources.hpp"

class GPUProfiler;
class Engine;

struct MeshRenderer
{
    Swift::IBuffer* m_vertex_buffer;
    Swift::IBufferSRV* m_vertex_buffer_srv;
    Swift::IBuffer* m_mesh_buffer;
    Swift::IBufferSRV* m_mesh_buffer_srv;
    Swift::IBuffer* m_mesh_vertex_buffer;
    Swift::IBufferSRV* m_mesh_vertex_buffer_srv;
    Swift::IBuffer* m_mesh_triangle_buffer;
    Swift::IBufferSRV* m_mesh_triangle_buffer_srv;
    uint32_t m_meshlet_count;
    int m_material_index;
    uint32_t m_transform_index;
    uint32_t m_bounding_offset;

    void Draw(Swift::ICommand* command, bool dispatch_amp) const;
};

struct GrassPatch
{
    glm::vec3 position;
    float height = 2.f;
    glm::vec2 padding;
    float width = 0.2f;
    float radius = 0.5;
};

struct DirectionalLight
{
    glm::vec3 direction;
    float intensity = 1.0f;
    glm::vec3 color = glm::vec3(1.0f);
    int cast_shadows = false;
    void SetDirectionEuler(const glm::vec3& euler)
    {
        const glm::mat4 rot = glm::yawPitchRoll(glm::radians(euler.y), glm::radians(euler.x), glm::radians(euler.z));
        constexpr auto forward = glm::vec3(0.0f, 0.0f, -1.0f);
        direction = glm::normalize(glm::vec3(rot * glm::vec4(forward, 0.0f)));
    }
};

struct PointLight
{
    glm::vec3 position;
    float intensity = 1.0f;
    glm::vec3 color = glm::vec3(1.0f);
    float range = 100.0f;
};

class Renderer
{
public:
    explicit Renderer(Engine* engine);
    ~Renderer();
    void UpdateGlobalConstantBuffer(const Camera& camera) const;

    void Update();
    auto* GetContext() const { return m_context; }
    void SetSkybox(Swift::ITexture* texture, Swift::ITexture* ibl_texture)
    {
        if (m_skybox_texture.texture)
        {
            m_context->GetGraphicsQueue()->WaitIdle();
            m_context->DestroyTexture(m_skybox_texture.texture);
            m_context->DestroyShaderResource(m_skybox_texture.texture_srv);
        }
        m_skybox_texture.texture = texture;
        m_skybox_texture.texture_srv = m_context->CreateShaderResource(m_skybox_texture.texture);

        if (m_specular_ibl_texture.texture)
        {
            m_context->GetGraphicsQueue()->WaitIdle();
            m_context->DestroyTexture(m_specular_ibl_texture.texture);
            m_context->DestroyShaderResource(m_specular_ibl_texture.texture_srv);
        }
        m_specular_ibl_texture.texture = ibl_texture;
        m_specular_ibl_texture.texture_srv = m_context->CreateShaderResource(m_specular_ibl_texture.texture);
    }
    std::tuple<uint32_t, uint32_t> AddRenderables(Model& model, const glm::mat4& transform)
    {
        auto result = CreateMeshRenderers(model, transform);
        m_transform_buffer->Write(m_transforms.data(), 0, sizeof(glm::mat4) * m_transforms.size());
        m_material_buffer->Write(m_materials.data(), 0, sizeof(Material) * m_materials.size());
        m_cull_data_buffer->Write(m_cull_data.data(), 0, sizeof(CullData) * m_cull_data.size());
        return result;
    }

    void AddPointLight(const PointLight& point_light)
    {
        m_point_lights.push_back(point_light);
        m_point_light_buffer->Write(&m_point_lights.back(), 0, sizeof(PointLight) * m_point_lights.size());
    }
    void AddDirectionalLight(const DirectionalLight& directional_light)
    {
        m_dir_lights.push_back(directional_light);

        glm::vec3 d = glm::normalize(directional_light.direction);
        glm::vec3 euler;
        euler.x = glm::degrees(asin(d.y));
        euler.y = glm::degrees(atan2(d.z, d.x));
        m_dir_light_eulers.push_back(euler);

        m_dir_light_buffer->Write(&m_dir_lights.back(), 0, sizeof(DirectionalLight) * m_dir_lights.size());
    }

    void GenerateStaticShadowMap() const;

private:
    void InitContext();
    void InitBuffers();
    void InitShadowPass();
    void InitSkyboxShader();
    void InitGrassPass();
    void InitVolumetricFog();
    void InitPBRShader();
    void DrawGeometry(Swift::ICommand* command) const;
    void DrawSkybox(Swift::ICommand* command) const;
    void DrawShadowPass(Swift::ICommand* command) const;
    void DrawGrassPass(Swift::ICommand* command) const;
    void DrawVolumetricFog(Swift::ICommand* command) const;
    void InitImgui();

    std::tuple<uint32_t, uint32_t> CreateMeshRenderers(Model& model, const glm::mat4& transform);

    std::unique_ptr<GPUProfiler> m_profiler;

    struct GlobalConstantInfo
    {
        glm::mat4 view_proj;
        glm::mat4 view;
        glm::mat4 proj;
        glm::mat4 sun_view_proj;

        glm::vec3 cam_pos;
        uint32_t cubemap_index;

        uint32_t transform_buffer_index;
        uint32_t material_buffer_index;
        uint32_t cull_data_buffer_index;
        uint32_t frustum_buffer_index;

        uint32_t point_light_buffer_index;
        uint32_t dir_light_buffer_index;
        uint32_t point_light_count;
        uint32_t dir_light_count;

        uint32_t shadow_texture_index;
        uint32_t grass_buffer_index;
    };

    struct TextureView
    {
        Swift::ITexture* texture;
        Swift::ITextureSRV* texture_srv;
    };

    Engine* m_engine;
    Swift::IContext* m_context = nullptr;
    Swift::IHeap* m_texture_heap = nullptr;

    bool m_rebuild_lights = false;

    TextureView m_dummy_white_texture;
    TextureView m_dummy_black_texture;
    TextureView m_dummy_normal_texture;
    TextureView m_specular_ibl_texture;

    Swift::ITexture* m_render_target_texture = nullptr;
    Swift::IRenderTarget* m_render_target = nullptr;
    Swift::ITextureSRV* m_render_target_srv = nullptr;
    Swift::ITexture* m_depth_texture = nullptr;
    Swift::IDepthStencil* m_depth_stencil = nullptr;
    Swift::ITextureSRV* m_depth_stencil_srv = nullptr;

    Swift::IBuffer* m_global_constant_buffer = nullptr;
    Swift::IBuffer* m_transform_buffer = nullptr;
    Swift::IBufferSRV* m_transform_buffer_srv = nullptr;
    Swift::IBuffer* m_material_buffer = nullptr;
    Swift::IBufferSRV* m_material_buffer_srv = nullptr;
    Swift::IBuffer* m_cull_data_buffer = nullptr;
    Swift::IBufferSRV* m_cull_data_buffer_srv = nullptr;
    Swift::IBuffer* m_frustum_buffer = nullptr;
    Swift::IBufferSRV* m_frustum_buffer_srv = nullptr;
    Swift::IBuffer* m_point_light_buffer = nullptr;
    Swift::IBufferSRV* m_point_light_buffer_srv = nullptr;
    Swift::IBuffer* m_dir_light_buffer = nullptr;
    Swift::IBufferSRV* m_dir_light_buffer_srv = nullptr;

    Swift::IShader* m_skybox_shader = nullptr;
    TextureView m_skybox_texture;

    float m_fog_density = 0.15f;
    float m_fog_max_distance = 200.f;
    glm::vec3 m_fog_color = glm::vec3(0.6, 0.65, 0.7);
    uint32_t m_raymarch_steps = 64;

    Swift::IShader* m_fog_shader = nullptr;
    Swift::ITexture* m_fog_texture = nullptr;
    Swift::IRenderTarget* m_fog_rtv = nullptr;
    Swift::ITextureSRV* m_fog_srv = nullptr;

    float m_wind_speed = 1.f;
    float m_wind_strength = 0.4f;
    float m_grass_lod_distance = 50.f;
    bool m_apply_view_space_thicken = false;
    Swift::IShader* m_grass_shader = nullptr;
    Swift::IBuffer* m_grass_buffer = nullptr;
    Swift::IBufferSRV* m_grass_buffer_srv = nullptr;
    std::vector<GrassPatch> m_grass_patches;

    Swift::IShader* m_shadow_shader = nullptr;
    Swift::ITexture* m_shadow_texture = nullptr;
    Swift::ITextureSRV* m_shadow_texture_srv = nullptr;
    Swift::IDepthStencil* m_shadow_depth_stencil = nullptr;

    Swift::IShader* m_pbr_shader = nullptr;
    std::vector<Swift::SamplerDescriptor> m_sampler_descriptors;
    std::vector<PointLight> m_point_lights;
    std::vector<DirectionalLight> m_dir_lights;
    std::vector<glm::vec3> m_dir_light_eulers;
    std::vector<MeshRenderer> m_renderables;
    std::vector<glm::mat4> m_transforms;
    std::vector<Material> m_materials;
    std::vector<CullData> m_cull_data;
    std::vector<TextureView> m_textures;
};
