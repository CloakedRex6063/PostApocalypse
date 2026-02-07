#pragma once

class Renderer;

class Resources
{
public:
    Resources(Renderer& renderer);
    ~Resources();

    void LoadModel(const std::filesystem::path& path);
    Swift::ITexture* LoadTexture(const std::filesystem::path& path) const;

private:
    Renderer& m_renderer;
};