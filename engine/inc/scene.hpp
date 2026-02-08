#pragma once
#include "actor.hpp"

template<typename T>
concept IsActor = std::derived_from<T, Actor> || std::is_base_of_v<Actor, T>;

class Scene
{
public:
    Scene(Engine* engine) : m_engine(engine) {};
    template<IsActor T>
    std::shared_ptr<Actor> AddActor();
    void Update(float dt) const;

private:
    Engine* m_engine;
    std::vector<std::shared_ptr<Actor>> m_actors;
};

template <IsActor T>
std::shared_ptr<Actor> Scene::AddActor()
{
    m_actors.emplace_back(std::make_shared<T>(m_engine));
    return m_actors.back();
}
