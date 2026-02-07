#include "engine.hpp"

Engine::Engine()
{
    m_window = std::make_unique<Window>();
    m_input = std::make_unique<Input>(*m_window);
    m_renderer = std::make_unique<Renderer>(*this);
    m_camera = std::make_unique<Camera>();
    m_resources = std::make_unique<Resources>(*m_renderer);

    m_renderer->SetSkybox(m_resources->LoadTexture("assets/skybox/sky.dds"));
}

Engine::~Engine()
{
}
