#include "scene.hpp"

void Scene::Update(const float dt) const
{
    for (const auto actor : m_actors)
    {
        actor->Update(dt);
    }
}