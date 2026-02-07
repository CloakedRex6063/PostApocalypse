#include "post_apocalyptic.hpp"

PostApocalyptic::PostApocalyptic(Engine *engine) : m_engine(engine)
{
}

void PostApocalyptic::Update(float dt)
{

}

int main()
{
    Engine engine;
    engine.Run<PostApocalyptic>();
}


