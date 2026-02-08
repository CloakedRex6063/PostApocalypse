#pragma once
#include "resources.hpp"

struct Transform
{
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
    glm::mat4 transform = glm::mat4(1.0f);
};

class Engine;

class Actor
{
public:
    Actor(Engine* engine) : m_engine(engine) {}
    ~Actor() {}
    void Update(float dt) {}
    void AddModel(Model& model);

private:
    Transform m_transform;
    std::string m_name;
    uint32_t m_instance_offset;
    uint32_t m_instance_size;
    Engine* m_engine;
};
