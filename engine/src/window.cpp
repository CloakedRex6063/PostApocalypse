#include "window.hpp"

Window::Window()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    m_window = glfwCreateWindow(1280, 720, "Window", nullptr, nullptr);
}

Window::~Window()
{
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

bool Window::IsRunning() const { return !glfwWindowShouldClose(m_window); }

void Window::PollEvents() const { glfwPollEvents(); }

void* Window::GetNativeWindow() const
{
    return glfwGetWin32Window(m_window);
}

GLFWwindow* Window::GetHandle() const { return m_window; }

glm::uvec2 Window::GetSize() const
{
    int width = 0, height = 0;
    glfwGetWindowSize(m_window, &width, &height);
    return {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
}

void Window::LockMouse(const bool toggle)
{
    m_is_locked = toggle;
    if (toggle)
    {
        glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }
    else
    {
        glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}
