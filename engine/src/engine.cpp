#include "engine.hpp"

Engine::Engine()
{
    m_window = std::make_unique<Window>();
    m_input = std::make_unique<Input>(*m_window);
    m_renderer = std::make_unique<Renderer>(this);
    m_camera = std::make_unique<Camera>(this);
    m_resources = std::make_unique<Resources>(this);
    m_scene = std::make_unique<Scene>(this);
}

Engine::~Engine() {}
