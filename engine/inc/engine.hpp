#pragma once
#include "camera.hpp"
#include "renderer.hpp"
#include "resources.hpp"
#include "scene.hpp"
#include "window.hpp"

class Engine;

template <typename T>
concept GameConcept =
    requires(Engine* engine, T obj, float dt) {
    T{engine};
    obj.Update(dt);
    };



class Engine
{
public:
    Engine();
    ~Engine();

    template<GameConcept game>
    void Run();

    auto& GetWindow() const { return *m_window; }
    auto& GetInput() const { return *m_input; }
    auto& GetRenderer() const { return *m_renderer; }
    auto& GetCamera() const { return *m_camera; }
    auto& GetResources() const { return *m_resources; }
    auto& GetScene() const { return *m_scene; }

private:
    std::unique_ptr<Window> m_window;
    std::unique_ptr<Input> m_input;
    std::unique_ptr<Camera> m_camera;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<Resources> m_resources;
    std::unique_ptr<Scene> m_scene;
};

template<GameConcept Game>
void Engine::Run()
{
    auto game = std::make_unique<Game>(this);
    auto prev_time = std::chrono::high_resolution_clock::now();
    while (m_window->IsRunning())
    {
        const auto current_time = std::chrono::high_resolution_clock::now();
        const auto delta_time = std::chrono::duration<float>(current_time - prev_time).count();
        prev_time = current_time;
        m_input->Update();
        m_window->PollEvents();
        m_camera->Update(delta_time);
        m_renderer->Update();
        m_scene->Update(delta_time);
        game->Update(delta_time);
    }
}
