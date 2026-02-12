#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "camera.hpp"
#include "engine.hpp"
#include "input.hpp"

glm::vec4 NormalizePlane(const glm::vec4 p)
{
    const float len = glm::length(glm::vec3(p));
    return p / len;
}

Frustum Camera::CreateFrustum() const
{
    auto vp = m_proj_matrix * m_view_matrix;
    vp = glm::transpose(vp);
    const Frustum frustum{ .planes = {
                               NormalizePlane(vp[3] + vp[0]),  // Left
                               NormalizePlane(vp[3] - vp[0]),  // Right
                               NormalizePlane(vp[3] + vp[1]),  // Bottom
                               NormalizePlane(vp[3] - vp[1]),  // Top
                               NormalizePlane(vp[2]),          // Near  (0-1 depth range)
                               NormalizePlane(vp[3] - vp[2]),  // Far
                           } };

    return frustum;
}

Frustum Camera::CreateFrustum(const glm::mat4& view_proj)
{
    auto vp = glm::transpose(view_proj);
    const Frustum frustum{ .planes = {
                               NormalizePlane(vp[3] + vp[0]),  // Left
                               NormalizePlane(vp[3] - vp[0]),  // Right
                               NormalizePlane(vp[3] + vp[1]),  // Bottom
                               NormalizePlane(vp[3] - vp[1]),  // Top
                               NormalizePlane(vp[2]),          // Near  (0-1 depth range)
                               NormalizePlane(vp[3] - vp[2]),  // Far
                           } };

    return frustum;
}

void Camera::Update(const float delta_time)
{
    auto& input = m_engine->GetInput();
    auto& window = m_engine->GetWindow();
    UpdateKeyboard(input, delta_time);
    UpdateMouse(window, m_engine->GetInput(), delta_time);

    const glm::mat4 translation = glm::translate(glm::mat4(1.0f), m_position);
    const glm::mat4 rotation_matrix = glm::toMat4(glm::quat(m_rotation));

    const auto size = window.GetSize();
    m_world_matrix = translation * rotation_matrix;
    m_aspect_ratio = (float)size.x / (float)size.y;
    m_view_matrix = glm::inverse(m_world_matrix);
    m_proj_matrix = glm::perspective(m_fov, m_aspect_ratio, m_near_plane, m_far_plane);
}

void Camera::UpdateKeyboard(Input& input, const float delta_time)
{
    const bool forward = input.IsKeyHeld(KeyboardKey::eW);
    const bool backward = input.IsKeyHeld(KeyboardKey::eS);
    const bool left = input.IsKeyHeld(KeyboardKey::eA);
    const bool right = input.IsKeyHeld(KeyboardKey::eD);
    const bool up = input.IsKeyHeld(KeyboardKey::eSpace);
    const bool down = input.IsKeyHeld(KeyboardKey::eLeftControl);

    float front_back = 0;
    float sideways = 0;

    if (forward)
    {
        front_back += m_move_speed;
    }
    if (backward)
    {
        front_back -= m_move_speed;
    }
    if (left)
    {
        sideways -= m_move_speed;
    }
    if (right)
    {
        sideways += m_move_speed;
    }
    m_position += ((GetForwardVector() * front_back) + (GetRightVector() * sideways)) * delta_time;
    if (up)
    {
        m_position.y += delta_time * m_move_speed;
    }
    if (down)
    {
        m_position.y -= delta_time * m_move_speed;
    }
}

void Camera::UpdateMouse(Window& window, Input& input, const float delta_time)
{
    if (input.IsMouseButtonHeld(MouseButton::eRight))
    {
        if (!window.IsMouseLocked())
        {
            window.LockMouse(true);
        }
    }
    else
    {
        window.LockMouse(false);
    }
    if (window.IsMouseLocked())
    {
        auto rot = glm::degrees(m_rotation);
        const auto delta = input.GetMouseDelta();
        rot.y -= delta.x * m_look_speed;
        rot.x += delta.y * m_look_speed;
        rot.x = glm::clamp(rot.x, -89.f, 89.f);
        m_rotation = glm::radians(rot);
    }
}
