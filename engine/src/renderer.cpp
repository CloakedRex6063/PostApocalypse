#include "renderer.hpp"
#include "engine.hpp"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "shader_data.hpp"
#include "imgui_impl_glfw.h"
#include "profiler.hpp"
#include "d3d12/d3d12_context.hpp"

void MeshRenderer::Draw(Swift::ICommand* command, const bool dispatch_amp) const
{
    if (dispatch_amp)
    {
        const uint32_t num_amp_groups = (m_meshlet_count + 31) / 32;
        command->DispatchMesh(num_amp_groups, 1, 1);
    }
    else
    {
        command->DispatchMesh(m_meshlet_count, 1, 1);
    }
}

Renderer::Renderer(Engine* engine) : m_engine(engine)
{
    InitContext();
    InitImgui();

    InitBuffers();
    InitPBRShader();
    InitShadowPass();
    InitGrassPass();
    InitSkyboxShader();
    InitVolumetricFog();

    m_profiler = std::make_unique<GPUProfiler>(m_context);

    m_engine->GetWindow().AddResizeCallback(
        [&](const glm::uvec2 size)
        {
            m_context->GetGraphicsQueue()->WaitIdle();
            m_context->ResizeBuffers(size.x, size.y);
            m_render_texture.Destroy(m_context);
            m_depth_texture.Destroy(m_context);
            m_post_process.m_dst_texture.Destroy(m_context);
            m_post_process.m_src_texture.Destroy(m_context);

            m_render_texture =
                TextureViewBuilder(m_context, size)
                    .SetFlags(EnumFlags(Swift::TextureFlags::eRenderTarget) | Swift::TextureFlags::eShaderResource)
                    .SetFormat(Swift::Format::eRGBA8_UNORM)
                    .Build();

            m_post_process.m_src_texture =
                TextureViewBuilder(m_context, size)
                    .SetFlags(EnumFlags(Swift::TextureFlags::eRenderTarget) | Swift::TextureFlags::eShaderResource)
                    .SetFormat(Swift::Format::eRGBA8_UNORM)
                    .Build();
            m_post_process.m_dst_texture =
                TextureViewBuilder(m_context, size)
                    .SetFlags(EnumFlags(Swift::TextureFlags::eRenderTarget) | Swift::TextureFlags::eShaderResource)
                    .SetFormat(Swift::Format::eRGBA8_UNORM)
                    .Build();

            m_depth_texture =
                TextureViewBuilder(m_context, size)
                    .SetFlags(EnumFlags(Swift::TextureFlags::eDepthStencil) | Swift::TextureFlags::eShaderResource)
                    .SetFormat(Swift::Format::eD32F)
                    .Build();
        });
}

Renderer::~Renderer()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    m_skybox_pass.texture.Destroy(m_context);
    Swift::DestroyContext(m_context);
}

void Renderer::UpdateGlobalConstantBuffer(const Camera& camera) const
{
    CPU_ZONE("Update Constant Buffer")
    constexpr float near_plane = 1.0f;
    constexpr float far_plane = 150.f;
    const glm::mat4 sun_proj = glm::orthoRH_ZO(-150.0f, 150.0f, -150.f, 150.0f, near_plane, far_plane);
    constexpr float sun_distance = 75.f;
    const glm::vec3 sun_pos = -glm::normalize(m_dir_lights[0].direction) * sun_distance;
    const auto sun_view = glm::lookAtRH(sun_pos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const GlobalConstantInfo info{
        .view_proj = camera.m_proj_matrix * camera.m_view_matrix,
        .view = camera.m_view_matrix,
        .proj = camera.m_proj_matrix,
        .sun_view_proj = sun_proj * sun_view,
        .cam_pos = camera.m_position,
        .cubemap_index = m_skybox_pass.texture.GetSRVDescriptorIndex(),
        .transform_buffer_index = m_transform_buffer.GetDescriptorIndex(),
        .material_buffer_index = m_material_buffer.GetDescriptorIndex(),
        .cull_data_buffer_index = m_cull_data_buffer.GetDescriptorIndex(),
        .frustum_buffer_index = m_frustum_buffer.GetDescriptorIndex(),
        .point_light_buffer_index = m_point_light_buffer.GetDescriptorIndex(),
        .dir_light_buffer_index = m_dir_light_buffer.GetDescriptorIndex(),
        .point_light_count = static_cast<uint32_t>(m_point_lights.size()),
        .dir_light_count = static_cast<uint32_t>(m_dir_lights.size()),
        .shadow_texture_index = m_shadow_pass.texture.GetSRVDescriptorIndex(),
        .grass_buffer_index = m_grass_pass.buffer.GetDescriptorIndex(),
    };
    m_global_constant_buffer->Write(&info, 0, sizeof(GlobalConstantInfo));
}
void Renderer::UpdateGrassDialog()
{
    if (ImGui::CollapsingHeader("Grass"))
    {
        ImGui::DragFloat("Wind Speed", &m_grass_pass.wind_speed);
        ImGui::DragFloat("Wind Strength", &m_grass_pass.wind_strength);
        ImGui::DragFloat("Grass LOD Distance", &m_grass_pass.lod_distance);
        ImGui::Checkbox("Apply View Space Thickening", &m_grass_pass.apply_view_space_thicken);

        if (ImGui::Button("Add Grass Patch"))
        {
            m_grass_pass.patches.emplace_back(GrassPatch{});
            m_grass_pass.buffer.Write(m_grass_pass.patches.data(), 0, sizeof(GrassPatch) * m_grass_pass.patches.size());
        }

        bool update_patches = false;
        for (uint32_t i = 0; i < m_grass_pass.patches.size(); ++i)
        {
            ImGui::PushID(("Grass " + std::to_string(i)).c_str());
            auto& patch = m_grass_pass.patches[i];
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
            m_grass_pass.buffer.Write(m_grass_pass.patches.data(), 0, sizeof(GrassPatch) * m_grass_pass.patches.size());
        }
    }
}
void Renderer::UpdateFogDialog()
{
    if (ImGui::CollapsingHeader("Volumetric Fog"))
    {
        ImGui::DragFloat("Fog Density", &m_fog_pass.density);
        ImGui::DragFloat("Fog Max Distance", &m_fog_pass.max_distance);
        ImGui::DragFloat3("Fog Color", glm::value_ptr(m_fog_pass.color));
        ImGui::DragInt("Ray March Steps", reinterpret_cast<int*>(&m_fog_pass.raymarch_steps));
        ImGui::DragFloat("Scattering Factor", &m_fog_pass.scattering_factor);
    }
}
void Renderer::UpdateLightsDialog(Camera& camera)
{
    ImGui::DragFloat("Move Speed", &camera.m_move_speed);
    if (ImGui::CollapsingHeader("Lights"))
    {
        if (ImGui::Button("Add Directional Light"))
        {
            AddDirectionalLight({});
        }

        for (int i = 0; i < m_dir_lights.size(); ++i)
        {
            auto& dir_light = m_dir_lights[i];
            auto& euler = m_dir_light_eulers[i];
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
            GenerateStaticShadowMap();
            m_dir_light_buffer.Write(&m_dir_lights.back(), 0, sizeof(DirectionalLight) * m_dir_lights.size());
            m_rebuild_lights = false;
        }

        ImGui::PopStyleColor(4);
        ImGui::End();
    }
}
void Renderer::RenderImGUI(Swift::ICommand* command, const Swift::ITexture* render_target_texture) const
{
    GPU_ZONE(m_profiler, command, "Imgui Pass");
    auto* render_target = m_context->GetCurrentRenderTarget();
    command->TransitionResource(render_target_texture->GetResource(), Swift::ResourceState::eRenderTarget);
    command->BindRenderTargets(render_target, m_depth_texture.depth_stencil);
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), static_cast<ID3D12GraphicsCommandList*>(command->GetCommandList()));
}
void Renderer::ClearTextures(Swift::ICommand* command) const
{
    GPU_ZONE(m_profiler, command, "Clear Textures")
    command->TransitionResource(m_render_texture.texture->GetResource(), Swift::ResourceState::eRenderTarget);
    command->ClearRenderTarget(m_render_texture.render_target, {});
    command->TransitionResource(m_depth_texture.texture->GetResource(), Swift::ResourceState::eDepthWrite);
    command->ClearDepthStencil(m_depth_texture.depth_stencil, 1.f, 0);
}
void Renderer::ImGUINewFrame()
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}
void Renderer::Update()
{
    CPU_ZONE("Rendering Loop");
    auto* command = m_context->GetCurrentCommand();

    auto& camera = m_engine->GetCamera();

    UpdateGlobalConstantBuffer(camera);

    {
        CPU_ZONE("Update Frustum Buffer")
        const auto frustum = camera.CreateFrustum();
        m_frustum_buffer.Write(&frustum, 0, sizeof(Frustum));
    }

    m_profiler->NewFrame();

    ImGUINewFrame();

    command->Begin();

    ClearTextures(command);

    DrawGeometry(command);
    DrawGrassPass(command);
    DrawSkybox(command);
    DrawVolumetricFog(command);

    ImGui::Begin("Debugging");
    UpdateLightsDialog(camera);
    UpdateGrassDialog();
    UpdateFogDialog();
    ImGui::End();

    auto* render_target_texture = m_context->GetCurrentSwapchainTexture();
    command->TransitionResource(m_post_process.m_dst_texture.texture->GetResource(), Swift::ResourceState::eCopySource);
    command->TransitionResource(render_target_texture->GetResource(), Swift::ResourceState::eCopyDest);
    command->CopyResource(m_post_process.m_dst_texture.texture->GetResource(), render_target_texture->GetResource());

    RenderImGUI(command, render_target_texture);

    command->TransitionResource(render_target_texture->GetResource(), Swift::ResourceState::ePresent);
    command->End();

    {
        CPU_ZONE("Present Time")
        m_context->Present(false);
    }
}

void Renderer::GenerateStaticShadowMap() const
{
    auto& camera = m_engine->GetCamera();
    UpdateGlobalConstantBuffer(camera);
    m_context->GetGraphicsQueue()->WaitIdle();
    const auto command = m_context->CreateCommand(Swift::QueueType::eGraphics);
    command->Begin();
    DrawShadowPass(command);
    command->End();
    const auto fence_value = m_context->GetGraphicsQueue()->Execute(command);
    m_context->GetGraphicsQueue()->Wait(fence_value);
}

void Renderer::InitContext()
{
    const auto size = m_engine->GetWindow().GetSize();
    m_context = Swift::CreateContext({
        .backend_type = Swift::BackendType::eD3D12,
        .width = size.x,
        .height = size.y,
        .native_window_handle = m_engine->GetWindow().GetNativeWindow(),
        .native_display_handle = nullptr,
        .cbv_srv_uav_handle_count = 16384,
    });

    constexpr auto white = 0xFFFFFFFF;
    m_dummy_white_texture = TextureViewBuilder(m_context, { 1, 1 })
                                .SetFlags(EnumFlags(Swift::TextureFlags::eShaderResource))
                                .SetFormat(Swift::Format::eRGBA8_UNORM)
                                .SetData(&white)
                                .Build();

    constexpr auto black = 0xFF000000;
    m_dummy_black_texture = TextureViewBuilder(m_context, { 1, 1 })
                                .SetFlags(EnumFlags(Swift::TextureFlags::eShaderResource))
                                .SetFormat(Swift::Format::eRGBA8_UNORM)
                                .SetData(&black)
                                .Build();

    constexpr auto normal = 0xFFFF8080;
    m_dummy_normal_texture = TextureViewBuilder(m_context, { 1, 1 })
                                 .SetFlags(EnumFlags(Swift::TextureFlags::eShaderResource))
                                 .SetFormat(Swift::Format::eRGBA8_UNORM)
                                 .SetData(&normal)
                                 .Build();

    m_render_texture = TextureViewBuilder(m_context, size)
                           .SetFlags(EnumFlags(Swift::TextureFlags::eRenderTarget) | Swift::TextureFlags::eShaderResource)
                           .SetFormat(Swift::Format::eRGBA8_UNORM)
                           .Build();

    m_depth_texture = TextureViewBuilder(m_context, size)
                          .SetFlags(EnumFlags(Swift::TextureFlags::eDepthStencil) | Swift::TextureFlags::eShaderResource)
                          .SetFormat(Swift::Format::eD32F)
                          .Build();

    m_post_process.m_src_texture =
        TextureViewBuilder(m_context, size)
            .SetFlags(EnumFlags(Swift::TextureFlags::eRenderTarget) | Swift::TextureFlags::eShaderResource)
            .SetFormat(Swift::Format::eRGBA8_UNORM)
            .Build();
    m_post_process.m_dst_texture =
        TextureViewBuilder(m_context, size)
            .SetFlags(EnumFlags(Swift::TextureFlags::eRenderTarget) | Swift::TextureFlags::eShaderResource)
            .SetFormat(Swift::Format::eRGBA8_UNORM)
            .Build();

    m_texture_heap = m_context->CreateHeap(
        Swift::HeapCreateInfo{ .type = Swift::HeapType::eGPU_Upload, .size = 2'500'000'000, .debug_name = "Texture Heap" });
}

void Renderer::InitBuffers()
{
    m_global_constant_buffer = Swift::BufferBuilder(m_context, 65536).Build();
    m_transform_buffer = BufferViewBuilder(m_context, 10'000 * sizeof(glm::mat4)).SetNumElements(10'000).Build();
    m_point_light_buffer = BufferViewBuilder(m_context, sizeof(PointLight) * 100).SetNumElements(100).Build();
    m_dir_light_buffer = BufferViewBuilder(m_context, sizeof(DirectionalLight) * 100).SetNumElements(100).Build();
    m_material_buffer = BufferViewBuilder(m_context, sizeof(Material) * 10'000).SetNumElements(10'000).Build();
    m_cull_data_buffer = BufferViewBuilder(m_context, sizeof(CullData) * 1'000'000).SetNumElements(1'000'000).Build();
    m_frustum_buffer = BufferViewBuilder(m_context, sizeof(Frustum)).SetNumElements(1).Build();
}

void Renderer::InitShadowPass()
{
    m_shadow_pass.texture = TextureViewBuilder(m_context, { 4096, 4096 })
                                .SetFormat(Swift::Format::eD32F)
                                .SetFlags(EnumFlags(Swift::TextureFlags::eDepthStencil) | Swift::TextureFlags::eShaderResource)
                                .SetName("Shadow Texture")
                                .Build();
    std::vector<Swift::Descriptor> descriptors{};
    const auto descriptor = Swift::DescriptorBuilder(Swift::DescriptorType::eConstant).SetShaderRegister(1).Build();
    descriptors.emplace_back(descriptor);
    const std::vector<Swift::SamplerDescriptor> samplers{
        {},
    };

    m_shadow_pass.shader = Swift::GraphicsShaderBuilder(m_context)
                               .SetRTVFormats({ Swift::Format::eRGBA8_UNORM })
                               .SetDSVFormat(Swift::Format::eD32F)
                               .SetMeshShader(shadow_mesh_main_code)
                               .SetPixelShader(shadow_pixel_main_code)
                               .SetDepthTestEnable(true)
                               .SetDepthWriteEnable(true)
                               .SetDepthBias(10)
                               .SetDepthBiasClamp(0.005f)
                               .SetSlopeScaledDepthBias(1.5f)
                               .SetCullMode(Swift::CullMode::eFront)
                               .SetDepthTest(Swift::DepthTest::eLess)
                               .SetPolygonMode(Swift::PolygonMode::eTriangle)
                               .SetDescriptors(descriptors)
                               .SetStaticSamplers(samplers)
                               .SetName("Shadow Shader")
                               .Build();
}

void Renderer::InitSkyboxShader()
{
    std::vector<Swift::Descriptor> descriptors{};
    const auto descriptor = Swift::DescriptorBuilder(Swift::DescriptorType::eConstant).SetShaderRegister(1).Build();
    descriptors.emplace_back(descriptor);
    const std::vector<Swift::SamplerDescriptor> samplers{ {} };

    m_skybox_pass.shader = Swift::GraphicsShaderBuilder(m_context)
                               .SetRTVFormats({ Swift::Format::eRGBA8_UNORM })
                               .SetDSVFormat(Swift::Format::eD32F)
                               .SetMeshShader(skybox_mesh_main_code)
                               .SetPixelShader(skybox_pixel_main_code)
                               .SetDepthTest(Swift::DepthTest::eGreaterEqual)
                               .SetDepthTestEnable(true)
                               .SetDepthWriteEnable(true)
                               .SetCullMode(Swift::CullMode::eNone)
                               .SetDepthTest(Swift::DepthTest::eLessEqual)
                               .SetPolygonMode(Swift::PolygonMode::eTriangle)
                               .SetDescriptors(descriptors)
                               .SetStaticSamplers(samplers)
                               .SetName("Skybox Shader")
                               .Build();
}

void Renderer::InitGrassPass()
{
    m_grass_pass.buffer = BufferViewBuilder(m_context, 10'000'000 * sizeof(GrassPatch))
                              .SetNumElements(10'000'000)
                              .SetName("Grass Patch Buffer")
                              .Build();

    std::vector<Swift::Descriptor> descriptors{};
    const auto descriptor = Swift::DescriptorBuilder(Swift::DescriptorType::eConstant).SetShaderRegister(1).Build();
    descriptors.emplace_back(descriptor);
    const std::vector<Swift::SamplerDescriptor> samplers{ {} };
    m_grass_pass.shader = Swift::GraphicsShaderBuilder(m_context)
                              .SetRTVFormats({ Swift::Format::eRGBA8_UNORM })
                              .SetDSVFormat(Swift::Format::eD32F)
                              .SetAmplificationShader(grass_ampl_main_code)
                              .SetMeshShader(grass_mesh_main_code)
                              .SetPixelShader(grass_pixel_main_code)
                              .SetCullMode(Swift::CullMode::eNone)
                              .SetDepthTestEnable(true)
                              .SetDepthWriteEnable(true)
                              .SetDepthTest(Swift::DepthTest::eLess)
                              .SetPolygonMode(Swift::PolygonMode::eTriangle)
                              .SetDescriptors(descriptors)
                              .SetStaticSamplers(samplers)
                              .SetName("Grass Shader")
                              .Build();
}

void Renderer::InitVolumetricFog()
{
    std::vector<Swift::Descriptor> descriptors{};
    const auto descriptor = Swift::DescriptorBuilder(Swift::DescriptorType::eConstant).SetShaderRegister(1).Build();
    descriptors.emplace_back(descriptor);
    const std::vector samplers{
        Swift::SamplerDescriptor{
            .min_filter = Swift::Filter::eNearest,
            .mag_filter = Swift::Filter::eNearest,
        },
        {},
        {
            .min_filter = Swift::Filter::eNearest,
            .mag_filter = Swift::Filter::eNearest,
            .wrap_u = Swift::Wrap::eBorder,
            .wrap_y = Swift::Wrap::eBorder,
            .wrap_w = Swift::Wrap::eBorder,
            .min_lod = 0,
            .max_lod = 13,
            .border_color = Swift::BorderColor::eWhite,
            .comparison_func = Swift::ComparisonFunc::eLess,
            .filter_type = Swift::FilterType::eComparison,
        },
    };
    m_fog_pass.shader = Swift::GraphicsShaderBuilder(m_context)
                            .SetRTVFormats({ Swift::Format::eRGBA8_UNORM })
                            .SetMeshShader(fog_mesh_main_code)
                            .SetPixelShader(fog_pixel_main_code)
                            .SetPolygonMode(Swift::PolygonMode::eTriangle)
                            .SetDescriptors(descriptors)
                            .SetStaticSamplers(samplers)
                            .SetName("Volumetric Fog Shader")
                            .Build();
}

void Renderer::InitPBRShader()
{
    std::vector<Swift::Descriptor> descriptors{};
    const auto descriptor = Swift::DescriptorBuilder(Swift::DescriptorType::eConstant).SetShaderRegister(1).Build();
    descriptors.emplace_back(descriptor);
    const std::vector<Swift::SamplerDescriptor> samplers{
        {},
        {
            .min_filter = Swift::Filter::eNearest,
            .mag_filter = Swift::Filter::eNearest,
            .wrap_u = Swift::Wrap::eBorder,
            .wrap_y = Swift::Wrap::eBorder,
            .wrap_w = Swift::Wrap::eBorder,
            .min_lod = 0,
            .max_lod = 13,
            .border_color = Swift::BorderColor::eWhite,
            .comparison_func = Swift::ComparisonFunc::eLess,
            .filter_type = Swift::FilterType::eComparison,
        },
    };

    m_pbr_shader = Swift::GraphicsShaderBuilder(m_context)
                       .SetRTVFormats({ Swift::Format::eRGBA8_UNORM })
                       .SetDSVFormat(Swift::Format::eD32F)
                       .SetAmplificationShader(model_ampl_main_code)
                       .SetMeshShader(model_mesh_main_code)
                       .SetPixelShader(model_pixel_main_code)
                       .SetDepthTestEnable(true)
                       .SetDepthWriteEnable(true)
                       .SetDepthTest(Swift::DepthTest::eLess)
                       .SetPolygonMode(Swift::PolygonMode::eTriangle)
                       .SetDescriptors(descriptors)
                       .SetStaticSamplers(samplers)
                       .SetName("PBR Shader")
                       .Build();
}

std::tuple<uint32_t, uint32_t> Renderer::CreateMeshRenderers(Model& model, const glm::mat4& transform)
{
    struct MeshBuffers
    {
        BufferView vertex_buffer;
        BufferView meshlet_buffer;
        BufferView meshlet_vertex_buffer;
        BufferView meshlet_tris_buffer;
    };

    std::vector<MeshBuffers> mesh_buffers;
    mesh_buffers.reserve(model.meshes.size());

    for (const auto& mesh : model.meshes)
    {
        MeshBuffers mesh_buffer;
        mesh_buffer.vertex_buffer = BufferViewBuilder(m_context, sizeof(Vertex) * mesh.vertices.size())
                                        .SetData(mesh.vertices.data())
                                        .SetNumElements(mesh.vertices.size())
                                        .Build();
        mesh_buffer.meshlet_buffer = BufferViewBuilder(m_context, sizeof(meshopt_Meshlet) * mesh.meshlets.size())
                                         .SetData(mesh.meshlets.data())
                                         .SetNumElements(mesh.meshlets.size())
                                         .Build();
        mesh_buffer.meshlet_vertex_buffer = BufferViewBuilder(m_context, sizeof(uint32_t) * mesh.meshlet_vertices.size())
                                                .SetData(mesh.meshlet_vertices.data())
                                                .SetNumElements(mesh.meshlet_vertices.size())
                                                .Build();
        mesh_buffer.meshlet_tris_buffer = BufferViewBuilder(m_context, sizeof(uint32_t) * mesh.meshlet_triangles.size())
                                              .SetData(mesh.meshlet_triangles.data())
                                              .SetNumElements(mesh.meshlet_triangles.size())
                                              .Build();
        mesh_buffers.push_back(mesh_buffer);
    }

    uint32_t heap_offset = 0;
    uint32_t texture_offset = m_textures.size();
    for (auto& texture : model.textures)
    {
        auto texture_builder = Swift::TextureBuilder(m_context, texture.width, texture.height)
                                   .SetFormat(texture.format)
                                   .SetArraySize(texture.array_size)
                                   .SetMipmapLevels(texture.mip_levels)
                                   .SetData(texture.pixels.data())
                                   .SetName(texture.name);
        auto info = texture_builder.GetBuildInfo();
        texture_builder.SetResource(m_texture_heap->CreateResource(info, heap_offset));
        heap_offset += m_context->CalculateAlignedTextureSize(info);
        auto* t = texture_builder.Build();

        auto* srv = m_context->CreateShaderResource(t);
        m_textures.emplace_back(TextureView{ t, srv });
    }

    for (auto& material : model.materials)
    {
        if (material.albedo_index != -1)
        {
            material.albedo_index = m_textures[texture_offset + material.albedo_index].GetSRVDescriptorIndex();
        }
        else
        {
            material.albedo_index = m_dummy_white_texture.GetSRVDescriptorIndex();
        }

        if (material.metal_rough_index != -1)
        {
            material.metal_rough_index = m_textures[texture_offset + material.metal_rough_index].GetSRVDescriptorIndex();
        }
        else
        {
            material.metal_rough_index = m_dummy_white_texture.GetSRVDescriptorIndex();
        }

        if (material.occlusion_index != -1)
        {
            material.occlusion_index = m_textures[texture_offset + material.occlusion_index].GetSRVDescriptorIndex();
        }
        else
        {
            material.occlusion_index = m_dummy_white_texture.GetSRVDescriptorIndex();
        }

        if (material.emissive_index != -1)
        {
            material.emissive_index = m_textures[texture_offset + material.emissive_index].GetSRVDescriptorIndex();
        }
        else
        {
            material.emissive_index = m_dummy_black_texture.GetSRVDescriptorIndex();
        }

        if (material.normal_index != -1)
        {
            material.normal_index = m_textures[texture_offset + material.normal_index].GetSRVDescriptorIndex();
        }
        else
        {
            material.normal_index = m_dummy_normal_texture.GetSRVDescriptorIndex();
        }
    }

    std::vector<MeshRenderer> renderers;
    renderers.reserve(model.nodes.size());
    uint32_t bounding_offset = m_cull_data.size();
    for (const auto& node : model.nodes)
    {
        const auto& mesh = model.meshes[node.mesh_index];
        const auto& [vertex_buffer, meshlet_buffer, meshlet_vertex_buffer, meshlet_tris_buffer] = mesh_buffers[node.mesh_index];
        const auto transform_index = static_cast<uint32_t>(m_transforms.size());
        auto final_transform = transform * model.transforms[node.transform_index];
        m_transforms.emplace_back(final_transform);
        const auto material_index = static_cast<int>(m_materials.size());
        m_materials.emplace_back(model.materials[mesh.material_index]);
        renderers.push_back({
            .m_vertex_buffer = vertex_buffer,
            .m_mesh_buffer = meshlet_buffer,
            .m_mesh_vertex_buffer = meshlet_vertex_buffer,
            .m_mesh_triangle_buffer = meshlet_tris_buffer,
            .m_meshlet_count = static_cast<uint32_t>(mesh.meshlets.size()),
            .m_material_index = material_index,
            .m_transform_index = transform_index,
            .m_bounding_offset = bounding_offset,
        });

        bounding_offset += mesh.meshlets.size();
    }
    m_cull_data.insert_range(m_cull_data.end(), model.cull_datas);
    auto offset = static_cast<uint32_t>(m_renderables.size());
    auto size = static_cast<uint32_t>(renderers.size());
    m_renderables.insert_range(m_renderables.end(), renderers);
    return { offset, size };
}

void Renderer::DrawGeometry(Swift::ICommand* command) const
{
    CPU_ZONE("Geometry Pass");
    GPU_ZONE(m_profiler, command, "Geometry Pass");
    const auto& window = m_engine->GetWindow();
    auto window_size = window.GetSize();
    const auto float_size = std::array{ static_cast<float>(window_size[0]), static_cast<float>(window_size[1]) };
    command->SetViewport(Swift::Viewport{ .dimensions = float_size });
    command->SetScissor(Swift::Scissor{ .dimensions = { window_size.x, window_size.y } });
    command->BindRenderTargets(m_render_texture.render_target, m_depth_texture.depth_stencil);
    command->BindShader(m_pbr_shader);
    command->BindConstantBuffer(m_global_constant_buffer, 1);

    for (const auto& renderable : m_renderables)
    {
        const struct PushConstants
        {
            uint32_t vertex_buffer;
            uint32_t meshlet_buffer;
            uint32_t mesh_vertex_buffer;
            uint32_t mesh_triangle_buffer;

            int material_index;
            uint32_t transform_index;
            uint32_t meshlet_count;
            uint32_t bounding_offset;

            uint32_t ibl_index;
        } push_constants{
            .vertex_buffer = renderable.m_vertex_buffer.GetDescriptorIndex(),
            .meshlet_buffer = renderable.m_mesh_buffer.GetDescriptorIndex(),
            .mesh_vertex_buffer = renderable.m_mesh_vertex_buffer.GetDescriptorIndex(),
            .mesh_triangle_buffer = renderable.m_mesh_triangle_buffer.GetDescriptorIndex(),
            .material_index = renderable.m_material_index,
            .transform_index = renderable.m_transform_index,
            .meshlet_count = renderable.m_meshlet_count,
            .bounding_offset = renderable.m_bounding_offset,
            .ibl_index = m_specular_ibl_texture.GetSRVDescriptorIndex(),
        };
        command->PushConstants(&push_constants, sizeof(PushConstants));
        renderable.Draw(command, true);
    }
}

void Renderer::DrawSkybox(Swift::ICommand* command) const
{
    CPU_ZONE("Skybox Pass");
    GPU_ZONE(m_profiler, command, "Skybox Pass");
    const auto& window = m_engine->GetWindow();
    auto window_size = window.GetSize();
    const auto float_size = std::array{ static_cast<float>(window_size[0]), static_cast<float>(window_size[1]) };
    command->SetViewport(Swift::Viewport{ .dimensions = float_size });
    command->SetScissor(Swift::Scissor{ .dimensions = { window_size.x, window_size.y } });
    command->BindRenderTargets(m_render_texture.render_target, m_depth_texture.depth_stencil);
    command->BindShader(m_skybox_pass.shader);
    command->BindConstantBuffer(m_global_constant_buffer, 1);
    command->DispatchMesh(1, 1, 1);
}

void Renderer::DrawShadowPass(Swift::ICommand* command) const
{
    CPU_ZONE("Shadow Pass");
    GPU_ZONE(m_profiler, command, "Shadow Pass");
    command->SetViewport(Swift::Viewport{ .dimensions = { 4096, 4096 } });
    command->SetScissor(Swift::Scissor{ .dimensions = { 4096, 4096 } });
    command->TransitionResource(m_shadow_pass.texture.texture->GetResource(), Swift::ResourceState::eDepthWrite);
    command->ClearDepthStencil(m_shadow_pass.texture.depth_stencil, 1.f, 0);
    command->BindRenderTargets(nullptr, m_shadow_pass.texture.depth_stencil);
    command->BindShader(m_shadow_pass.shader);
    command->BindConstantBuffer(m_global_constant_buffer, 1);

    for (const auto& renderable : m_renderables)
    {
        const struct PushConstants
        {
            uint32_t vertex_buffer;
            uint32_t meshlet_buffer;
            uint32_t mesh_vertex_buffer;
            uint32_t mesh_triangle_buffer;

            int material_index;
            uint32_t transform_index;
            uint32_t meshlet_count;
            uint32_t bounding_offset;

            uint32_t ibl_index;
        } push_constants{
            .vertex_buffer = renderable.m_vertex_buffer.GetDescriptorIndex(),
            .meshlet_buffer = renderable.m_mesh_buffer.GetDescriptorIndex(),
            .mesh_vertex_buffer = renderable.m_mesh_vertex_buffer.GetDescriptorIndex(),
            .mesh_triangle_buffer = renderable.m_mesh_triangle_buffer.GetDescriptorIndex(),
            .material_index = renderable.m_material_index,
            .transform_index = renderable.m_transform_index,
            .meshlet_count = renderable.m_meshlet_count,
            .bounding_offset = renderable.m_bounding_offset,
            .ibl_index = m_specular_ibl_texture.GetSRVDescriptorIndex(),
        };
        command->PushConstants(&push_constants, sizeof(PushConstants));
        renderable.Draw(command, false);
    }
    command->TransitionResource(m_shadow_pass.texture.texture->GetResource(), Swift::ResourceState::eShaderResource);
}

void Renderer::DrawGrassPass(Swift::ICommand* command) const
{
    if (m_grass_pass.patches.empty()) return;
    CPU_ZONE("Grass Pass");
    GPU_ZONE(m_profiler, command, "Grass Pass");
    const auto& window = m_engine->GetWindow();
    auto window_size = window.GetSize();
    const auto float_size = std::array{ static_cast<float>(window_size[0]), static_cast<float>(window_size[1]) };
    command->SetViewport(Swift::Viewport{ .dimensions = float_size });
    command->SetScissor(Swift::Scissor{ .dimensions = { window_size.x, window_size.y } });
    command->BindRenderTargets(m_render_texture.render_target, m_render_texture.depth_stencil);
    command->BindShader(m_grass_pass.shader);
    command->BindConstantBuffer(m_global_constant_buffer, 1);

    const struct PushConstant
    {
        float wind_speed;
        float wind_strength;
        uint32_t apply_view_space_thicken;
        float lod_distance;

        uint32_t grass_count;
        float time;
    } pc{
        .wind_speed = m_grass_pass.wind_speed,
        .wind_strength = m_grass_pass.wind_strength,
        .apply_view_space_thicken = m_grass_pass.apply_view_space_thicken,
        .lod_distance = m_grass_pass.lod_distance,
        .grass_count = static_cast<uint32_t>(m_grass_pass.patches.size()),
        .time = m_engine->GetTime(),
    };
    command->PushConstants(&pc, sizeof(PushConstant));

    const uint32_t num_amp_groups = (m_grass_pass.patches.size() + 31 / 32);
    command->DispatchMesh(num_amp_groups, 1, 1);
}

void Renderer::DrawVolumetricFog(Swift::ICommand* command)
{
    m_post_process.Swap();
    command->TransitionResource(m_render_texture.texture->GetResource(), Swift::ResourceState::eShaderResource);
    command->TransitionResource(m_depth_texture.texture->GetResource(), Swift::ResourceState::eShaderResource);
    command->TransitionResource(m_post_process.m_dst_texture.texture->GetResource(), Swift::ResourceState::eRenderTarget);
    auto& camera = m_engine->GetCamera();
    const auto& window = m_engine->GetWindow();
    auto window_size = window.GetSize();
    const auto float_size = std::array{ static_cast<float>(window_size[0]), static_cast<float>(window_size[1]) };
    command->SetViewport(Swift::Viewport{ .dimensions = float_size });
    command->SetScissor(Swift::Scissor{ .dimensions = { window_size.x, window_size.y } });
    command->BindShader(m_fog_pass.shader);
    command->BindConstantBuffer(m_global_constant_buffer, 1);
    command->BindRenderTargets(m_post_process.m_dst_texture.render_target, nullptr);
    const struct PushConstant
    {
        glm::mat4 inv_view_proj;
        uint32_t scene_texture_index;
        uint32_t depth_texture_index;
        float fog_density;
        float fog_max_distance;

        glm::vec3 fog_color;
        uint32_t ray_march_steps;
        uint32_t shadow_texture_index;
        float scattering_factor;
    } pc{
        .inv_view_proj = glm::inverse(camera.m_proj_matrix * camera.m_view_matrix),
        .scene_texture_index = m_render_texture.GetSRVDescriptorIndex(),
        .depth_texture_index = m_depth_texture.GetSRVDescriptorIndex(),
        .fog_density = m_fog_pass.density,
        .fog_max_distance = m_fog_pass.max_distance,
        .fog_color = m_fog_pass.color,
        .ray_march_steps = m_fog_pass.raymarch_steps,
        .shadow_texture_index = m_shadow_pass.texture.GetSRVDescriptorIndex(),
        .scattering_factor = m_fog_pass.scattering_factor,
    };
    command->PushConstants(&pc, sizeof(PushConstant));
    command->DispatchMesh(1, 1, 1);
}

void Renderer::InitImgui() const
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui_ImplGlfw_InitForOther(m_engine->GetWindow().GetHandle(), true);

    const auto* dx_context = static_cast<Swift::D3D12::Context*>(m_context);
    auto* srv_heap = dx_context->GetCBVSRVUAVHeap();

    ImGui_ImplDX12_InitInfo init_info = {};
    init_info.Device = static_cast<ID3D12Device*>(m_context->GetDevice());
    init_info.CommandQueue = static_cast<ID3D12CommandQueue*>(m_context->GetGraphicsQueue()->GetQueue());
    init_info.NumFramesInFlight = 1;
    init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    init_info.UserData = srv_heap;
    init_info.SrvDescriptorHeap = srv_heap->GetHeap();
    init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info,
                                        D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle,
                                        D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle)
    {
        auto* srv_heap = static_cast<Swift::D3D12::DescriptorHeap*>(info->UserData);
        const auto descriptor = srv_heap->Allocate();
        out_cpu_handle->ptr = descriptor.cpu_handle.ptr;
        out_gpu_handle->ptr = descriptor.gpu_handle.ptr;
    };
    init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info,
                                       const D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
                                       const D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle)
    {
        auto* srv_heap = static_cast<Swift::D3D12::DescriptorHeap*>(info->UserData);
        const uint32_t index = (cpu_handle.ptr - srv_heap->GetCpuBaseHandle().ptr) / srv_heap->GetStride();
        srv_heap->Free(Swift::D3D12::DescriptorData{ .cpu_handle = cpu_handle, .gpu_handle = gpu_handle, .index = index });
    };
    ImGui_ImplDX12_Init(&init_info);

    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.16f, 0.16f, 0.16f, 0.50f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.19f, 0.19f, 0.19f, 0.92f);
    colors[ImGuiCol_Border] = ImVec4(0.19f, 0.19f, 0.19f, 0.29f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.24f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.259f, 0.588f, 0.980f, 1.000f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.40f, 0.54f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
    colors[ImGuiCol_Button] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 0.00f, 0.00f, 0.36f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.22f, 0.23f, 0.33f);
    colors[ImGuiCol_Separator] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.20f, 0.20f, 0.20f, 0.52f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_DockingPreview] = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    colors[ImGuiCol_PlotLines] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 0.00f, 0.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(1.00f, 0.00f, 0.00f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(1.00f, 0.00f, 0.00f, 0.35f);

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(5.00f, 0.00f);
    style.FramePadding = ImVec2(4.00f, 4.00f);
    style.CellPadding = ImVec2(4.00f, 4.00f);
    style.ItemSpacing = ImVec2(6.00f, 6.00f);
    style.ItemInnerSpacing = ImVec2(6.00f, 6.00f);
    style.TouchExtraPadding = ImVec2(0.00f, 0.00f);
    style.IndentSpacing = 25;
    style.ScrollbarSize = 15;
    style.GrabMinSize = 10;
    style.WindowBorderSize = 1;
    style.ChildBorderSize = 1;
    style.PopupBorderSize = 1;
    style.FrameBorderSize = 1;
    style.TabBorderSize = 1;
    style.WindowRounding = 7;
    style.ChildRounding = 4;
    style.FrameRounding = 3;
    style.PopupRounding = 4;
    style.ScrollbarRounding = 9;
    style.GrabRounding = 3;
    style.LogSliderDeadzone = 4;
    style.TabRounding = 4;
}
