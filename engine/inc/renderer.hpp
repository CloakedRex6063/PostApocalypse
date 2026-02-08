#pragma once
#include "resources.hpp"

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

    void Draw(Swift::ICommand* command, const bool should_cull = false, const uint32_t pass_index = 0) const
    {
        const struct PushConstants
        {
            uint32_t vertex_buffer;
            uint32_t meshlet_buffer;
            uint32_t mesh_vertex_buffer;
            uint32_t mesh_triangle_buffer;
            int material_index;
            uint32_t transform_index;
            uint32_t meshlet_count;
            uint32_t bounding_offset;
            uint32_t should_cull;
            uint32_t pass_index;
        } push_constants{
            .vertex_buffer = m_vertex_buffer_srv->GetDescriptorIndex(),
            .meshlet_buffer = m_mesh_buffer_srv->GetDescriptorIndex(),
            .mesh_vertex_buffer = m_mesh_vertex_buffer_srv->GetDescriptorIndex(),
            .mesh_triangle_buffer = m_mesh_triangle_buffer_srv->GetDescriptorIndex(),
            .material_index = m_material_index,
            .transform_index = m_transform_index,
            .meshlet_count = m_meshlet_count,
            .bounding_offset = m_bounding_offset,
            .should_cull = should_cull,
            .pass_index = pass_index,
        };
        command->PushConstants(&push_constants, sizeof(PushConstants));
        // const uint32_t num_amp_groups = (m_meshlet_count + 31) / 32;
        command->DispatchMesh(m_meshlet_count, 1, 1);
    }
};

struct DirectionalLight
{
    glm::vec3 direction;
    float intensity = 1.0f;
    glm::vec3 color = glm::vec3(1.0f);
    int cast_shadows = false;
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

    void Update() const;
    auto* GetContext() const { return m_context; }
    void SetSkybox(Swift::ITexture* texture)
    {
        if (m_skybox_texture.texture)
        {
            m_context->GetGraphicsQueue()->WaitIdle();
            m_context->DestroyTexture(m_skybox_texture.texture);
            m_context->DestroyShaderResource(m_skybox_texture.texture_srv);
        }
        m_skybox_texture.texture = texture;
        m_skybox_texture.texture_srv = m_context->CreateShaderResource(m_skybox_texture.texture);
    }
    std::tuple<uint32_t, uint32_t> AddRenderables(Model& model, const glm::mat4& transform)
    {
        auto result = CreateMeshRenderers(model, transform);
        m_transform_buffer->Write(m_transforms.data(), 0, sizeof(glm::mat4) * m_transforms.size());
        m_material_buffer->Write(m_materials.data(), 0, sizeof(Material) * m_materials.size());
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
        m_dir_light_buffer->Write(&m_dir_lights.back(), 0, sizeof(DirectionalLight) * m_dir_lights.size());
    }

private:
    void InitContext();
    void InitBuffers();
    void InitSkyboxShader();
    void InitPBRShader();

    std::tuple<uint32_t, uint32_t> CreateMeshRenderers(Model& model, const glm::mat4& transform);

    struct GlobalConstantInfo
    {
        glm::mat4 view_proj;
        glm::mat4 view;
        glm::mat4 proj;

        glm::vec3 cam_pos;
        uint32_t cubemap_index;

        uint32_t transform_buffer_index;
        uint32_t material_buffer_index;
        uint32_t point_light_buffer_index;
        uint32_t dir_light_buffer_index;
        uint32_t point_light_count;
        uint32_t dir_light_count;
    };

    struct TextureView
    {
        Swift::ITexture* texture;
        Swift::ITextureSRV* texture_srv;
    };

    Engine* m_engine;
    Swift::IContext* m_context = nullptr;
    Swift::IHeap* m_texture_heap = nullptr;

    TextureView m_skybox_texture;
    TextureView m_dummy_white_texture;
    TextureView m_dummy_black_texture;
    TextureView m_dummy_normal_texture;

    Swift::ITexture* m_depth_texture = nullptr;
    Swift::IDepthStencil* m_depth_stencil = nullptr;
    Swift::IBuffer* m_global_constant_buffer = nullptr;
    Swift::IBuffer* m_transform_buffer = nullptr;
    Swift::IBufferSRV* m_transform_buffer_srv = nullptr;
    Swift::IBuffer* m_material_buffer = nullptr;
    Swift::IBufferSRV* m_material_buffer_srv = nullptr;
    Swift::IBuffer* m_point_light_buffer = nullptr;
    Swift::IBufferSRV* m_point_light_buffer_srv = nullptr;
    Swift::IBuffer* m_dir_light_buffer = nullptr;
    Swift::IBufferSRV* m_dir_light_buffer_srv = nullptr;

    Swift::IShader* m_skybox_shader = nullptr;
    Swift::IShader* m_pbr_shader = nullptr;
    std::vector<Swift::SamplerDescriptor> m_sampler_descriptors;
    std::vector<PointLight> m_point_lights;
    std::vector<DirectionalLight> m_dir_lights;
    std::vector<MeshRenderer> m_renderables;
    std::vector<glm::mat4> m_transforms;
    std::vector<Material> m_materials;
    std::vector<TextureView> m_textures;
};
