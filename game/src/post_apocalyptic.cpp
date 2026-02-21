#include "post_apocalyptic.hpp"

PostApocalyptic::PostApocalyptic(Engine *engine) : m_engine(engine)
{
    auto* skybox_texture = m_engine->GetResources().LoadTexture("assets/skybox/sky.dds");
    auto* ibl_texture = m_engine->GetResources().LoadTexture("assets/skybox/sky_specular.dds");
    m_engine->GetRenderer().SetSkybox(skybox_texture, ibl_texture);
    DirectionalLight dir_light{
        .intensity = 3.f,
        .color = glm::vec3(1.0f, 0.75f, 0.3f),
        .cast_shadows = true,
    };
    dir_light.SetDirectionEuler(glm::vec3(-20.0f, 135.0f, 0.0f));
    m_engine->GetRenderer().AddDirectionalLight(dir_light);
    m_engine->GetResources().LoadModel("assets/cathedral/cathedral.gltf", glm::vec3(1.f));
    m_engine->GetRenderer().GenerateStaticShadowMap();
}

void PostApocalyptic::Update(float dt)
{

}

int main()
{
    Engine engine;
    engine.Run<PostApocalyptic>();
}


