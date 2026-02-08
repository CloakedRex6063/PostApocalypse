#include "actor.hpp"
#include "engine.hpp"

void Actor::AddModel(Model& model)
{
    auto [offset, size] = m_engine->GetRenderer().AddRenderables(model, m_transform.transform);
    m_instance_size = size;
    m_instance_offset = offset;
}