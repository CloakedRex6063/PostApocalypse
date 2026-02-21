#pragma once
#include "camera.hpp"
#include "resources.hpp"

class GPUProfiler;
class Engine;

struct TextureView
{
    Swift::ITexture* texture;
    Swift::ITextureSRV* srv;
    Swift::ITextureUAV* uav;
    Swift::IRenderTarget* render_target;
    Swift::IDepthStencil* depth_stencil;

    uint32_t GetSRVDescriptorIndex() const { return srv->GetDescriptorIndex(); }

    void Destroy(Swift::IContext* context) const
    {
        if (texture)
        {
            context->DestroyTexture(texture);
        }
        if (srv)
        {
            context->DestroyShaderResource(srv);
        }
        if (render_target)
        {
            context->DestroyRenderTarget(render_target);
        }
        if (depth_stencil)
        {
            context->DestroyDepthStencil(depth_stencil);
        }
        if (uav)
        {
            context->DestroyUnorderedAccessView(uav);
        }
    }
};

class TextureViewBuilder
{
public:
    TextureViewBuilder(Swift::IContext* context, const glm::vec2 size)
        : m_context(context), m_texture_builder(context, size.x, size.y)
    {
    }
    TextureViewBuilder& SetFlags(const EnumFlags<Swift::TextureFlags> texture_flags)
    {
        m_texture_builder.SetFlags(texture_flags);
        return *this;
    }

    TextureViewBuilder& SetMipmapLevels(const uint32_t levels)
    {
        m_texture_builder.SetMipmapLevels(levels);
        return *this;
    }

    TextureViewBuilder& SetGenMipMaps(const bool gen_mipmaps)
    {
        m_texture_builder.SetGenMipMaps(gen_mipmaps);
        return *this;
    }

    TextureViewBuilder& SetArraySize(const uint32_t array_size)
    {
        m_texture_builder.SetArraySize(array_size);
        return *this;
    }

    TextureViewBuilder& SetFormat(const Swift::Format format)
    {
        m_texture_builder.SetFormat(format);
        return *this;
    }

    TextureViewBuilder& SetData(const void* data)
    {
        m_texture_builder.SetData(data);
        return *this;
    }

    TextureViewBuilder& SetMSAA(const Swift::MSAA msaa)
    {
        m_texture_builder.SetMSAA(msaa);
        return *this;
    }

    TextureViewBuilder& SetName(const std::string_view name)
    {
        m_texture_builder.SetName(name);
        return *this;
    }

    TextureView Build() const
    {
        const auto build_info = m_texture_builder.GetBuildInfo();
        TextureView texture_view{};
        texture_view.texture = m_texture_builder.Build();
        if (build_info.flags & Swift::TextureFlags::eRenderTarget)
        {
            texture_view.render_target = m_context->CreateRenderTarget(texture_view.texture);
        }
        if (build_info.flags & Swift::TextureFlags::eDepthStencil)
        {
            texture_view.depth_stencil = m_context->CreateDepthStencil(texture_view.texture);
        }
        if (build_info.flags & Swift::TextureFlags::eShaderResource)
        {
            texture_view.srv = m_context->CreateShaderResource(texture_view.texture);
        }
        if (build_info.flags & Swift::TextureFlags::eUnorderedAccess)
        {
            texture_view.uav = m_context->CreateUnorderedAccessView(texture_view.texture);
        }
        return texture_view;
    }

private:
    Swift::IContext* m_context;
    Swift::TextureBuilder m_texture_builder;
};

struct BufferView
{
    Swift::IBuffer* buffer;
    Swift::IBufferSRV* srv;

    uint32_t GetDescriptorIndex() const { return srv->GetDescriptorIndex(); }

    void Write(const void* data, const uint32_t offset, const uint32_t size, const bool one_time = false) const
    {
        buffer->Write(data, offset, size, one_time);
    }

    void Destroy(Swift::IContext* context) const
    {
        if (buffer)
        {
            context->DestroyBuffer(buffer);
        }
        if (srv)
        {
            context->DestroyShaderResource(srv);
        }
    }
};

class BufferViewBuilder
{
public:
    BufferViewBuilder(Swift::IContext* context, const uint32_t size) : m_context(context), m_builder(context, size) {}

    BufferViewBuilder& SetData(const void* data)
    {
        m_builder.SetData(data);
        return *this;
    }

    BufferViewBuilder& SetNumElements(const uint32_t num_elements)
    {
        m_num_elements = num_elements;
        return *this;
    }

    BufferViewBuilder& SetBufferType(const Swift::BufferType buffer_type)
    {
        m_builder.SetBufferType(buffer_type);
        return *this;
    }

    BufferViewBuilder& SetName(const std::string_view name)
    {
        m_builder.SetName(name);
        return *this;
    }

    BufferView Build() const
    {
        BufferView buffer_view{};
        buffer_view.buffer = m_builder.Build();
        if (m_num_elements)
        {
            buffer_view.srv =
                m_context->CreateShaderResource(buffer_view.buffer,
                                                Swift::BufferSRVCreateInfo{
                                                    .num_elements = m_num_elements,
                                                    .element_size = m_builder.GetBuildInfo().size / m_num_elements,
                                                    .first_element = 0,
                                                });
        }
        return buffer_view;
    }

private:
    Swift::IContext* m_context;
    Swift::BufferBuilder m_builder;
    uint32_t m_num_elements = 0;
};

struct MeshRenderer
{
    BufferView m_vertex_buffer;
    BufferView m_mesh_buffer;
    BufferView m_mesh_vertex_buffer;
    BufferView m_mesh_triangle_buffer;
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
    void UpdateGrassDialog();
    void UpdateFogDialog();
    void UpdateLightsDialog(Camera& camera);
    void RenderImGUI(Swift::ICommand* command, Swift::ITexture* render_target_texture) const;
    void ClearTextures(Swift::ICommand* command) const;
    static void ImGUINewFrame();

    void Update();
    auto* GetContext() const { return m_context; }
    void SetSkybox(Swift::ITexture* texture, Swift::ITexture* ibl_texture)
    {
        if (m_skybox_pass.texture.texture)
        {
            m_context->GetGraphicsQueue()->WaitIdle();
            m_context->DestroyTexture(m_skybox_pass.texture.texture);
            m_context->DestroyShaderResource(m_skybox_pass.texture.srv);
        }
        m_skybox_pass.texture.texture = texture;
        m_skybox_pass.texture.srv = m_context->CreateShaderResource(m_skybox_pass.texture.texture);

        if (m_specular_ibl_texture.texture)
        {
            m_context->GetGraphicsQueue()->WaitIdle();
            m_context->DestroyTexture(m_specular_ibl_texture.texture);
            m_context->DestroyShaderResource(m_specular_ibl_texture.srv);
        }
        m_specular_ibl_texture.texture = ibl_texture;
        m_specular_ibl_texture.srv = m_context->CreateShaderResource(m_specular_ibl_texture.texture);
    }

    std::tuple<uint32_t, uint32_t> AddRenderables(Model& model, const glm::mat4& transform)
    {
        auto result = CreateMeshRenderers(model, transform);
        m_transform_buffer.buffer->Write(m_transforms.data(), 0, sizeof(glm::mat4) * m_transforms.size());
        m_material_buffer.buffer->Write(m_materials.data(), 0, sizeof(Material) * m_materials.size());
        m_cull_data_buffer.buffer->Write(m_cull_data.data(), 0, sizeof(CullData) * m_cull_data.size());
        return result;
    }

    void AddPointLight(const PointLight& point_light)
    {
        m_point_lights.push_back(point_light);
        m_point_light_buffer.buffer->Write(&m_point_lights.back(), 0, sizeof(PointLight) * m_point_lights.size());
    }
    void AddDirectionalLight(const DirectionalLight& directional_light)
    {
        m_dir_lights.push_back(directional_light);

        const glm::vec3 d = glm::normalize(directional_light.direction);
        glm::vec3 euler;
        euler.x = glm::degrees(asin(d.y));
        euler.y = glm::degrees(atan2(d.z, d.x));
        m_dir_light_eulers.push_back(euler);

        m_dir_light_buffer.buffer->Write(&m_dir_lights.back(), 0, sizeof(DirectionalLight) * m_dir_lights.size());
    }

    void GenerateStaticShadowMap() const;

private:
    void InitContext();
    void InitBuffers();
    void InitDepthPrepass();
    void InitShadowPass();
    void InitSSAOPass();
    void InitSkyboxShader();
    void InitGrassPass();
    void InitVolumetricFog();
    void InitGeometryShader();
    void InitBloomPass();
    void InitTonemapPass();
    void DrawDepthPrePass(Swift::ICommand* command) const;
    void DrawGeometry(Swift::ICommand* command) const;
    void DrawSSAOPass(Swift::ICommand* command) const;
    void DrawSkybox(Swift::ICommand* command) const;
    void DrawShadowPass(Swift::ICommand* command) const;
    void DrawGrassPass(Swift::ICommand* command) const;
    void DrawBloomPass(Swift::ICommand* command);
    void DrawVolumetricFog(Swift::ICommand* command);
    void DrawTonemapPass(Swift::ICommand* command);
    void InitImgui() const;

    std::tuple<uint32_t, uint32_t> CreateMeshRenderers(Model& model, const glm::mat4& transform);

    std::unique_ptr<GPUProfiler> m_profiler;

    struct GlobalConstantInfo
    {
        glm::mat4 view_proj;
        glm::mat4 view;
        glm::mat4 proj;
        glm::mat4 sun_view_proj;
        glm::mat4 inv_view_proj;
        glm::mat4 inv_proj;

        glm::vec3 cam_pos;
        uint32_t cubemap_index;

        float fog_density;
        float fog_max_distance;
        float scattering_coefficient;
        float absorption_coefficient;

        glm::vec3 fog_color;
        uint32_t ray_march_steps;

        glm::vec3 absorption_color;
        float scattering_factor;

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
        uint32_t ibl_texture_index;
        uint32_t ssao_texture_index;

        glm::vec2 screen_size;
        glm::vec2 inv_screen_size;
    };

    Engine* m_engine;
    Swift::IContext* m_context = nullptr;

    bool m_rebuild_lights = false;

    TextureView m_dummy_white_texture;
    TextureView m_dummy_black_texture;
    TextureView m_dummy_normal_texture;
    TextureView m_specular_ibl_texture;

    TextureView m_render_texture;
    TextureView m_depth_texture;

    std::array<BufferView, 3> m_global_constant_buffers;
    BufferView m_transform_buffer;
    BufferView m_material_buffer;
    BufferView m_cull_data_buffer;
    BufferView m_frustum_buffer;
    BufferView m_point_light_buffer;
    BufferView m_dir_light_buffer;
    Swift::ISampler* m_bilinear_sampler;
    Swift::ISampler* m_shadow_comparison_sampler;
    Swift::ISampler* m_nearest_sampler;

    struct DepthPrePass
    {
        Swift::IShader* shader = nullptr;
    } m_depth_prepass;

    struct SkyboxPass
    {
        Swift::IShader* shader = nullptr;
        TextureView texture;
    } m_skybox_pass;

    struct SSAOPass
    {
        Swift::IShader* gen_shader = nullptr;
        Swift::IShader* blur_shader = nullptr;
        TextureView gen_texture;
        TextureView blur_texture;
        TextureView noise_texture;
        BufferView kernel_buffer;
    } m_ssao_pass;

    struct TonemapPass
    {
        Swift::IShader* shader = nullptr;
        float exposure = 1.0f;
    } m_tonemap_pass;

    struct FogPass
    {
        float density = 0.02f;
        float max_distance = 400.f;
        float scattering_factor = 0.6f;
        glm::vec3 scattering_color = glm::vec3(0.6, 0.65, 0.7);
        glm::vec3 absorption_color = glm::vec3(0.05, 0.2, 0.8);
        float scattering_coefficient = 0.3f;
        float absorption_coefficient = 0.7f;
        uint32_t raymarch_steps = 32;
        Swift::IShader* shader = nullptr;
    } m_fog_pass;

    struct BloomPass
    {
        Swift::IShader* extract_shader;
        Swift::IShader* blur_shader;
        Swift::IShader* combine_shader;
        uint32_t blur_count = 10;
    } m_bloom_pass;

    struct PostProcess
    {
        TextureView m_src_texture;
        TextureView m_dst_texture;

        void Swap() { std::swap(m_src_texture, m_dst_texture); }
    } m_post_process_hdr;

    PostProcess m_post_process_ldr;

    struct GrassPass
    {
        float wind_speed = 1.f;
        float wind_strength = 0.4f;
        float lod_distance = 50.f;
        bool apply_view_space_thicken = false;
        Swift::IShader* shader = nullptr;
        BufferView buffer;
        std::vector<GrassPatch> patches;
    } m_grass_pass;

    struct ShadowPass
    {
        Swift::IShader* shader = nullptr;
        TextureView texture;
    } m_shadow_pass;

    Swift::IShader* m_pbr_shader = nullptr;
    std::vector<PointLight> m_point_lights;
    std::vector<DirectionalLight> m_dir_lights;
    std::vector<glm::vec3> m_dir_light_eulers;
    std::vector<MeshRenderer> m_renderables;
    std::vector<glm::mat4> m_transforms;
    std::vector<Material> m_materials;
    std::vector<CullData> m_cull_data;
    std::vector<TextureView> m_textures;
};
