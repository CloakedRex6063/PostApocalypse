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
    [[nodiscard]] Transform GetTransform() const { return m_transform; }
    [[nodiscard]] glm::vec3 GetPosition() const { return m_transform.position; }
    [[nodiscard]] glm::vec3 GetRotation() const { return m_transform.rotation; }
    [[nodiscard]] glm::vec3 GetScale() const { return m_transform.scale; }

private:
    Transform m_transform;
    std::string m_name;
    uint32_t m_instance_offset;
    uint32_t m_instance_size;
    Engine* m_engine;
};
