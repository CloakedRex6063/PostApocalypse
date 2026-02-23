#include "editor.hpp"
#include "engine.hpp"
#include "imgui.h"

void Editor::Render(const uint64_t* image_handle)
{
    ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::Begin("Viewport ",
                 nullptr,
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::Image(*image_handle, ImGui::GetContentRegionAvail());

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ModelPayload"))
        {
            const auto model_location = (const char*)payload->Data;

            const auto& camera = m_engine->GetCamera();
            const auto transform = camera.m_position + camera.GetForwardVector();
            m_engine->GetResources().LoadModel(model_location, transform, glm::vec3(1.f));
        }

        ImGui::EndDragDropTarget();
    }

    ImGui::End();
    UpdateRenderSettings();
    m_content_browser.Render();
}

void Editor::UpdateRenderSettings()
{
    ImGui::Begin("Render Settings");
    auto& renderer = m_engine->GetRenderer();
    auto& grass_pass = renderer.GetGrassPass();
    if (ImGui::CollapsingHeader("Grass"))
    {
        ImGui::DragFloat("Wind Speed", &grass_pass.wind_speed);
        ImGui::DragFloat("Wind Strength", &grass_pass.wind_strength);
        ImGui::DragFloat("Grass LOD Distance", &grass_pass.lod_distance);
        ImGui::Checkbox("Apply View Space Thickening", &grass_pass.apply_view_space_thicken);

        if (ImGui::Button("Add Grass Patch"))
        {
            grass_pass.patches.emplace_back(GrassPatch{});
            grass_pass.buffer.Write(grass_pass.patches.data(), 0, sizeof(GrassPatch) * grass_pass.patches.size());
        }

        bool update_patches = false;
        for (uint32_t i = 0; i < grass_pass.patches.size(); ++i)
        {
            ImGui::PushID(("Grass " + std::to_string(i)).c_str());
            auto& patch = grass_pass.patches[i];
            if (ImGui::DragFloat3("Position", glm::value_ptr(patch.position)))
            {
                update_patches = true;
            }
            if (ImGui::DragFloat("Height", &patch.height))
            {
                update_patches = true;
            }
            if (ImGui::DragFloat("Radius", &patch.radius))
            {
                update_patches = true;
            }
            ImGui::PopID();
        }
        if (update_patches)
        {
            grass_pass.buffer.Write(grass_pass.patches.data(), 0, sizeof(GrassPatch) * grass_pass.patches.size());
        }
    }

    auto& fog_pass = renderer.GetFogPass();
    if (ImGui::CollapsingHeader("Volumetric Fog"))
    {
        ImGui::DragFloat("Fog Density", &fog_pass.density);
        ImGui::DragFloat("Fog Max Distance", &fog_pass.max_distance);
        ImGui::DragFloat3("Scattering Fog Color", glm::value_ptr(fog_pass.scattering_color));
        ImGui::DragFloat3("Absorption Fog Color", glm::value_ptr(fog_pass.absorption_color));
        ImGui::DragInt("Ray March Steps", reinterpret_cast<int*>(&fog_pass.raymarch_steps));
        ImGui::DragFloat("Scattering Factor", &fog_pass.scattering_factor);
        ImGui::DragFloat("Scattering Coefficient", &fog_pass.scattering_coefficient);
        ImGui::DragFloat("Absorption Coefficient", &fog_pass.absorption_coefficient);
    }

    auto& camera = m_engine->GetCamera();
    ImGui::DragFloat("Move Speed", &camera.m_move_speed);
    auto dir_lights = renderer.GetDirectionalLights();
    if (ImGui::CollapsingHeader("Lights"))
    {
        if (ImGui::Button("Add Directional Light"))
        {
            renderer.AddDirectionalLight({});
        }

        for (int i = 0; i < dir_lights.size(); ++i)
        {
            auto& dir_light = dir_lights[i];
            auto& euler = renderer.m_dir_light_eulers[i];
            ImGui::PushID(i);
            if (ImGui::SliderFloat3("Light Rotation (Euler)", glm::value_ptr(euler), -180.0f, 180.0f))
            {
                glm::mat4 rot = glm::yawPitchRoll(glm::radians(euler.y), glm::radians(euler.x), glm::radians(euler.z));
                auto forward = glm::vec3(0.0f, 0.0f, -1.0f);
                dir_light.direction = glm::normalize(glm::vec3(rot * glm::vec4(forward, 0.0f)));
                m_rebuild_lights = true;
            }

            if (ImGui::DragFloat("Intensity", &dir_light.intensity))
            {
                m_rebuild_lights = true;
            }
            if (ImGui::DragFloat3("Color", glm::value_ptr(dir_light.color)))
            {
                m_rebuild_lights = true;
            }
            ImGui::PopID();
        }
    }

    if (m_rebuild_lights)
    {
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                           ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar;

        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 10, 10), ImGuiCond_Always, ImVec2(1, 0));

        ImGui::Begin("##rebuild_alert", nullptr, flags);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.35f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.45f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.50f, 0.25f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.0f, 1.0f));

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().FramePadding.y);
        ImGui::Text("Lighting needs to be rebuilt [!]");
        ImGui::SameLine();
        if (ImGui::Button("Rebuild Lights"))
        {
            m_engine->GetRenderer().GenerateStaticShadowMap();
            renderer.m_dir_light_buffer.Write(&renderer.m_dir_lights.back(),
                                              0,
                                              sizeof(DirectionalLight) * renderer.m_dir_lights.size());
            m_rebuild_lights = false;
        }

        ImGui::PopStyleColor(4);
        ImGui::End();
    }

    if (ImGui::CollapsingHeader("Tonemap Pass"))
    {
        ImGui::DragFloat("Exposure", &renderer.m_tonemap_pass.exposure);
    }
    ImGui::End();
}

void ContentBrowser::Render()
{
    ImGui::Begin("Content Browser");
    static auto currentPath = std::filesystem::current_path().string();
    const auto basePath = std::filesystem::current_path().string();
    const ImVec2 mainSize = ImGui::GetContentRegionAvail();
    const ImVec2 childSize(mainSize.x * 0.15f, mainSize.y);

    ImGui::BeginChild("Folders", childSize, true);

    DrawRoot(basePath, currentPath);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("Content Browser", ImVec2(mainSize.x * 0.85f, mainSize.y), true);

    ImGui::SetCursorPosX(50.f);

    constexpr int columnCount = 6;
    if (ImGui::BeginTable("##hidden", columnCount))
    {
        int columnIndex = 0;

        int id = 0;
        for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator(currentPath))
        {
            ImGui::PushID(id);
            if (columnIndex == 0) ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(columnIndex);

            std::string name = entry.path().filename().string();

            ImGui::Button(name.c_str(), ImVec2(100, 100));

            if (entry.path().extension() == ".glb" || entry.path().extension() == ".gltf")
            {
                auto path = std::filesystem::relative(entry.path().string(), std::filesystem::current_path()).string();
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID) && entry.is_regular_file())
                {
                    ImGui::SetDragDropPayload("ModelPayload", path.c_str(), path.size() + 1, ImGuiCond_Once);
                    ImGui::EndDragDropSource();
                }
            }

            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                if (entry.is_directory())
                {
                    currentPath = entry.path().string();
                }
            }

            ImGui::Text(name.c_str());

            columnIndex = (columnIndex + 1) % columnCount;

            ImGui::PopID();
            id++;
        }
        ImGui::EndTable();
    }

    ImGui::EndChild();

    ImGui::End();
}

bool ContentBrowser::DrawRoot(const std::string_view path, std::string& selected_folder)
{
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;

    if (path == selected_folder)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    const auto name = std::string(" ") + "Assets";
    bool opened = ImGui::TreeNodeEx(name.c_str(), flags);

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
    {
        selected_folder = path;
    }

    if (opened)
    {
        DrawFolderTree(path, selected_folder, opened);
        ImGui::TreePop();
    }
    return opened;
}

void ContentBrowser::DrawFolderTree(const std::filesystem::path& folder_path, std::string& selected_folder, bool root_open)
{
    if (!root_open) return;

    for (const auto& entry : std::filesystem::directory_iterator(folder_path))
    {
        if (entry.is_directory())
        {
            std::string folderName = entry.path().filename().string();
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;

            if (entry.path().string() == selected_folder)
            {
                flags |= ImGuiTreeNodeFlags_Selected;
            }

            auto name = std::string(" ") + folderName;
            bool opened = ImGui::TreeNodeEx(name.c_str(), flags);

            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
            {
                selected_folder = entry.path().string();
            }

            if (opened)
            {
                DrawFolderTree(entry.path(), selected_folder, root_open);
                ImGui::TreePop();
            }
        }
    }
}