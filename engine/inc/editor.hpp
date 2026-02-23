#pragma once

class Engine;

class ContentBrowser
{
public:
    static void Render();

private:
    static bool DrawRoot(std::string_view path, std::string& selected_folder);
    static void DrawFolderTree(const std::filesystem::path& folder_path, std::string& selected_folder, bool root_open);
};


class Editor
{
public:
    Editor(Engine* engine) : m_engine(engine) {}
    void Render(const uint64_t* image_handle);

private:
    void UpdateRenderSettings();
    Engine* m_engine;
    bool m_rebuild_lights = false;
    ContentBrowser m_content_browser;
};