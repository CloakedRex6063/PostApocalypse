#pragma once

class Engine;

class Renderer
{
public:
    explicit Renderer(Engine& engine);
    ~Renderer();

    void Update() const;
    auto* GetContext() const { return m_context; }
    void SetSkybox(Swift::ITexture* texture)
    {
        if (m_skybox)
        {
            m_context->GetGraphicsQueue()->WaitIdle();
            m_context->DestroyTexture(m_skybox);
            m_context->DestroyShaderResource(m_skybox_srv);
        }
        m_skybox = texture;
        m_skybox_srv = m_context->CreateShaderResource(m_skybox);
    }

private:
    Engine& m_engine;
    Swift::IContext* m_context = nullptr;
    Swift::ITexture* m_skybox = nullptr;
    Swift::ITextureSRV* m_skybox_srv = nullptr;
    Swift::ITexture* m_depth_texture = nullptr;
    Swift::IDepthStencil* m_depth_stencil = nullptr;
    Swift::IBuffer* m_global_constant_buffer = nullptr;
    Swift::IShader* m_skybox_shader = nullptr;

    struct GlobalConstantInfo
    {
        glm::mat4 view_proj;
        glm::mat4 view;
        glm::mat4 proj;
        uint32_t cubemap_index;
    };
};
