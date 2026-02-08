#include "engine.hpp"

Engine::Engine()
{
    m_window = std::make_unique<Window>();
    m_input = std::make_unique<Input>(*m_window);
    m_renderer = std::make_unique<Renderer>(this);
    m_camera = std::make_unique<Camera>(this);
    m_resources = std::make_unique<Resources>(this);
    m_scene = std::make_unique<Scene>(this);

    m_renderer->AddDirectionalLight(DirectionalLight{
        .direction = glm::normalize(glm::vec3(-0.5f, -0.3f, -0.8f)),
        .intensity = 1.f,
        .color = glm::vec3(1.0f, 0.95f, 0.8f),
        .cast_shadows = true,
    });
    m_resources->LoadModel("assets/helmet.gltf");

    m_renderer->SetSkybox(m_resources->LoadTexture("assets/skybox/sky.dds"));
}

Engine::~Engine()
{
}
