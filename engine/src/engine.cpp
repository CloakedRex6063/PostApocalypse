#include "engine.hpp"

Engine::Engine()
{
    m_window = std::make_unique<Window>();
    m_input = std::make_unique<Input>(*m_window);
    m_renderer = std::make_unique<Renderer>(this);
    m_camera = std::make_unique<Camera>(this);
    m_resources = std::make_unique<Resources>(this);
    m_scene = std::make_unique<Scene>(this);

    auto* skybox_texture = m_resources->LoadTexture("assets/skybox/sky.dds");
    auto* ibl_texture = m_resources->LoadTexture("assets/skybox/sky_specular.dds");
    m_renderer->SetSkybox(skybox_texture, ibl_texture);
    DirectionalLight dir_light{
        .intensity = 1.f,
        .color = glm::vec3(1.0f, 0.75f, 0.3f),
        .cast_shadows = true,
    };
    dir_light.SetDirectionEuler(glm::vec3(-45.0f, 135.0f, 0.0f));
    m_renderer->AddDirectionalLight(dir_light);
    m_resources->LoadModel("assets/cathedral/cathedral.gltf", glm::vec3(1.f));
    m_renderer->GenerateStaticShadowMap();
}

Engine::~Engine() {}
