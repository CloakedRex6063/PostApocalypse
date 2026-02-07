#pragma once
#include "engine.hpp"

class PostApocalyptic
{
public:
    PostApocalyptic(Engine* engine);
    void Update(float dt);

private:
    Engine* m_engine;
};
