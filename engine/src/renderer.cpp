#include "renderer.hpp"
#include "engine.hpp"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "shader_data.hpp"
#include "imgui_impl_glfw.h"
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
    InitSkyboxShader();

    m_engine->GetWindow().AddResizeCallback(
        [&](const glm::uvec2 size)
        {
            m_context->GetGraphicsQueue()->WaitIdle();
            m_context->ResizeBuffers(size.x, size.y);
            m_context->DestroyDepthStencil(m_depth_stencil);
            m_context->DestroyTexture(m_depth_texture);

            m_depth_texture = Swift::TextureBuilder(m_context, size.x, size.y)
                                  .SetFlags(EnumFlags(Swift::TextureFlags::eDepthStencil))
                                  .SetFormat(Swift::Format::eD32F)
                                  .SetMipmapLevels(0)
                                  .Build();
            m_depth_stencil = m_context->CreateDepthStencil(m_depth_texture);
        });
}

Renderer::~Renderer()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (m_skybox_texture.texture)
    {
        m_context->DestroyTexture(m_skybox_texture.texture);
        m_context->DestroyShaderResource(m_skybox_texture.texture_srv);
    }
    Swift::DestroyContext(m_context);
}

void Renderer::UpdateGlobalConstantBuffer(const Camera& camera) const
{
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
        .cubemap_index = m_skybox_texture.texture_srv->GetDescriptorIndex(),
        .transform_buffer_index = m_transform_buffer_srv->GetDescriptorIndex(),
        .material_buffer_index = m_material_buffer_srv->GetDescriptorIndex(),
        .cull_data_buffer_index = m_cull_data_buffer_srv->GetDescriptorIndex(),
        .frustum_buffer_index = m_frustum_buffer_srv->GetDescriptorIndex(),
        .point_light_buffer_index = m_point_light_buffer_srv->GetDescriptorIndex(),
        .dir_light_buffer_index = m_dir_light_buffer_srv->GetDescriptorIndex(),
        .point_light_count = static_cast<uint32_t>(m_point_lights.size()),
        .dir_light_count = static_cast<uint32_t>(m_dir_lights.size()),
        .shadow_texture_index = m_shadow_texture_srv->GetDescriptorIndex(),
    };
    m_global_constant_buffer->Write(&info, 0, sizeof(GlobalConstantInfo));
}
void Renderer::Update()
{
    auto* render_target = m_context->GetCurrentRenderTarget();
    auto* command = m_context->GetCurrentCommand();

    auto& camera = m_engine->GetCamera();

    UpdateGlobalConstantBuffer(camera);

    const auto frustum = camera.CreateFrustum();
    m_frustum_buffer->Write(&frustum, 0, sizeof(Frustum));

    m_dir_light_buffer->Write(&m_dir_lights.back(), 0, sizeof(DirectionalLight) * m_dir_lights.size());

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    command->Begin();

    command->TransitionResource(render_target->GetTexture()->GetResource(), Swift::ResourceState::eRenderTarget);
    command->ClearRenderTarget(render_target, { 0.392f, 0.584f, 0.929f, 1.0 });
    command->TransitionResource(m_depth_stencil->GetTexture()->GetResource(), Swift::ResourceState::eDepthWrite);
    command->ClearDepthStencil(m_depth_stencil, 1.f, 0);
    command->BindRenderTargets(render_target, m_depth_stencil);

    DrawSkybox(command);

    DrawGeometry(command);

    ImGui::Begin("Lights");
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
            glm::vec3 forward = glm::vec3(0.0f, 0.0f, -1.0f);
            dir_light.direction = glm::normalize(glm::vec3(rot * glm::vec4(forward, 0.0f)));
            m_rebuild_lights = true;
        }

        ImGui::DragFloat("Intensity", &dir_light.intensity);
        ImGui::DragFloat3("Color", glm::value_ptr(dir_light.color));
        ImGui::PopID();
    }

    ImGui::DragFloat("Move Speed", &camera.m_move_speed);

    ImGui::End();

    if (m_rebuild_lights) {
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar;

        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 10, 10), ImGuiCond_Always, ImVec2(1, 0));

        ImGui::Begin("##rebuild_alert", nullptr, flags);

        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.65f, 0.35f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.45f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.50f, 0.25f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1.0f,  0.85f, 0.0f, 1.0f));

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().FramePadding.y);
        ImGui::Text("Lighting needs to be rebuilt [!]");
        ImGui::SameLine();
        if (ImGui::Button("Rebuild Lights")) {
            GenerateStaticShadowMap();
            m_rebuild_lights = false;
        }

        ImGui::PopStyleColor(4);
        ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), static_cast<ID3D12GraphicsCommandList*>(command->GetCommandList()));

    command->TransitionResource(render_target->GetTexture()->GetResource(), Swift::ResourceState::ePresent);
    command->End();
    m_context->Present(false);
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
    m_context = Swift::CreateContext({ .backend_type = Swift::BackendType::eD3D12,
                                       .width = size.x,
                                       .height = size.y,
                                       .native_window_handle = m_engine->GetWindow().GetNativeWindow(),
                                       .native_display_handle = nullptr });

    constexpr auto white = 0xFFFFFFFF;
    m_dummy_white_texture.texture = Swift::TextureBuilder(m_context, 1, 1)
                                        .SetFlags(EnumFlags(Swift::TextureFlags::eShaderResource))
                                        .SetFormat(Swift::Format::eRGBA8_UNORM)
                                        .SetData(&white)
                                        .Build();
    m_dummy_white_texture.texture_srv = m_context->CreateShaderResource(m_dummy_white_texture.texture);

    constexpr auto black = 0xFF000000;
    m_dummy_black_texture.texture = Swift::TextureBuilder(m_context, 1, 1)
                                        .SetFlags(EnumFlags(Swift::TextureFlags::eShaderResource))
                                        .SetFormat(Swift::Format::eRGBA8_UNORM)
                                        .SetData(&black)
                                        .Build();
    m_dummy_black_texture.texture_srv = m_context->CreateShaderResource(m_dummy_black_texture.texture);

    constexpr auto normal = 0xFFFF8080;
    m_dummy_normal_texture.texture = Swift::TextureBuilder(m_context, 1, 1)
                                         .SetFlags(EnumFlags(Swift::TextureFlags::eShaderResource))
                                         .SetFormat(Swift::Format::eRGBA8_UNORM)
                                         .SetData(&normal)
                                         .Build();
    m_dummy_normal_texture.texture_srv = m_context->CreateShaderResource(m_dummy_normal_texture.texture);

    m_depth_texture = Swift::TextureBuilder(m_context, size.x, size.y)
                          .SetFlags(EnumFlags(Swift::TextureFlags::eDepthStencil))
                          .SetFormat(Swift::Format::eD32F)
                          .Build();
    m_depth_stencil = m_context->CreateDepthStencil(m_depth_texture);
    m_texture_heap = m_context->CreateHeap(
        Swift::HeapCreateInfo{ .type = Swift::HeapType::eGPU_Upload, .size = 2'500'000'000, .debug_name = "Texture Heap" });
}

void Renderer::InitBuffers()
{
    m_global_constant_buffer = Swift::BufferBuilder(m_context, 65536).Build();
    m_transform_buffer = Swift::BufferBuilder(m_context, 10'000 * sizeof(glm::mat4)).Build();
    m_transform_buffer_srv = m_context->CreateShaderResource(m_transform_buffer,
                                                             Swift::BufferSRVCreateInfo{
                                                                 .num_elements = 10'000,
                                                                 .element_size = sizeof(glm::mat4),
                                                                 .first_element = 0,
                                                             });
    m_point_light_buffer = Swift::BufferBuilder(m_context, sizeof(PointLight) * 100).Build();
    m_point_light_buffer_srv = m_context->CreateShaderResource(m_point_light_buffer,
                                                               Swift::BufferSRVCreateInfo{
                                                                   .num_elements = 100,
                                                                   .element_size = sizeof(PointLight),
                                                               });
    m_dir_light_buffer = Swift::BufferBuilder(m_context, sizeof(DirectionalLight) * 100).Build();
    m_dir_light_buffer_srv = m_context->CreateShaderResource(m_dir_light_buffer,
                                                             Swift::BufferSRVCreateInfo{
                                                                 .num_elements = 100,
                                                                 .element_size = sizeof(DirectionalLight),
                                                             });
    m_material_buffer = Swift::BufferBuilder(m_context, sizeof(Material) * 10'000).Build();
    m_material_buffer_srv = m_context->CreateShaderResource(m_material_buffer,
                                                            Swift::BufferSRVCreateInfo{
                                                                .num_elements = 10'000,
                                                                .element_size = sizeof(Material),
                                                            });
    m_material_buffer = Swift::BufferBuilder(m_context, sizeof(Material) * 10'000).Build();
    m_material_buffer_srv = m_context->CreateShaderResource(m_material_buffer,
                                                            Swift::BufferSRVCreateInfo{
                                                                .num_elements = 10'000,
                                                                .element_size = sizeof(Material),
                                                            });
    m_cull_data_buffer = Swift::BufferBuilder(m_context, 1'000'000 * sizeof(CullData)).Build();
    m_cull_data_buffer_srv = m_context->CreateShaderResource(m_cull_data_buffer,
                                                             Swift::BufferSRVCreateInfo{
                                                                 .num_elements = 1'000'000,
                                                                 .element_size = sizeof(CullData),
                                                                 .first_element = 0,
                                                             });
    m_frustum_buffer = Swift::BufferBuilder(m_context, sizeof(Frustum) * 5).Build();
    m_frustum_buffer_srv = m_context->CreateShaderResource(m_frustum_buffer,
                                                           Swift::BufferSRVCreateInfo{
                                                               .num_elements = 5,
                                                               .element_size = sizeof(Frustum),
                                                               .first_element = 0,
                                                           });
}

void Renderer::InitShadowPass()
{
    m_shadow_texture = Swift::TextureBuilder(m_context, 4096, 4096)
                           .SetFormat(Swift::Format::eD32F)
                           .SetFlags(Swift::TextureFlags::eDepthStencil)
                           .SetName("Shadow Texture")
                           .Build();
    m_shadow_depth_stencil = m_context->CreateDepthStencil(m_shadow_texture);
    m_shadow_texture_srv = m_context->CreateShaderResource(m_shadow_texture);
    std::vector<Swift::Descriptor> descriptors{};
    const auto descriptor = Swift::DescriptorBuilder(Swift::DescriptorType::eConstant).SetShaderRegister(1).Build();
    descriptors.emplace_back(descriptor);
    const std::vector<Swift::SamplerDescriptor> samplers{
        {},
    };

    m_shadow_shader = Swift::GraphicsShaderBuilder(m_context)
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

    m_skybox_shader = Swift::GraphicsShaderBuilder(m_context)
                          .SetRTVFormats({ Swift::Format::eRGBA8_UNORM })
                          .SetDSVFormat(Swift::Format::eD32F)
                          .SetMeshShader(skybox_mesh_main_code)
                          .SetPixelShader(skybox_pixel_main_code)
                          .SetDepthTestEnable(true)
                          .SetDepthWriteEnable(false)
                          .SetCullMode(Swift::CullMode::eNone)
                          .SetDepthTest(Swift::DepthTest::eLessEqual)
                          .SetPolygonMode(Swift::PolygonMode::eTriangle)
                          .SetDescriptors(descriptors)
                          .SetStaticSamplers(samplers)
                          .SetName("Skybox Shader")
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
    auto CreateBufferSRV = [this](const void* data, const size_t count, const size_t element_size)
    {
        auto* buffer = Swift::BufferBuilder(m_context, element_size * count).SetData(data).Build();
        auto* srv =
            m_context->CreateShaderResource(buffer,
                                            Swift::BufferSRVCreateInfo{ .num_elements = static_cast<uint32_t>(count),
                                                                        .element_size = static_cast<uint32_t>(element_size) });
        return std::pair{ buffer, srv };
    };

    struct MeshBuffers
    {
        Swift::IBuffer* vertex_buffer;
        Swift::IBufferSRV* vertex_srv;
        Swift::IBuffer* meshlet_buffer;
        Swift::IBufferSRV* meshlet_srv;
        Swift::IBuffer* meshlet_vertex_buffer;
        Swift::IBufferSRV* meshlet_vertex_srv;
        Swift::IBuffer* meshlet_tris_buffer;
        Swift::IBufferSRV* meshlet_tris_srv;
    };

    std::vector<MeshBuffers> mesh_buffers;
    mesh_buffers.reserve(model.meshes.size());

    for (const auto& mesh : model.meshes)
    {
        auto [vb, vs] = CreateBufferSRV(mesh.vertices.data(), mesh.vertices.size(), sizeof(Vertex));
        auto [mb, ms] = CreateBufferSRV(mesh.meshlets.data(), mesh.meshlets.size(), sizeof(meshopt_Meshlet));
        auto [mvb, mvs] = CreateBufferSRV(mesh.meshlet_vertices.data(), mesh.meshlet_vertices.size(), sizeof(uint32_t));
        auto [mtb, mts] = CreateBufferSRV(mesh.meshlet_triangles.data(), mesh.meshlet_triangles.size(), sizeof(uint32_t));

        mesh_buffers.push_back({ vb, vs, mb, ms, mvb, mvs, mtb, mts });
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
            material.albedo_index = m_textures[texture_offset + material.albedo_index].texture_srv->GetDescriptorIndex();
        }
        else
        {
            material.albedo_index = m_dummy_white_texture.texture_srv->GetDescriptorIndex();
        }

        if (material.metal_rough_index != -1)
        {
            material.metal_rough_index =
                m_textures[texture_offset + material.metal_rough_index].texture_srv->GetDescriptorIndex();
        }
        else
        {
            material.metal_rough_index = m_dummy_white_texture.texture_srv->GetDescriptorIndex();
        }

        if (material.occlusion_index != -1)
        {
            material.occlusion_index = m_textures[texture_offset + material.occlusion_index].texture_srv->GetDescriptorIndex();
        }
        else
        {
            material.occlusion_index = m_dummy_white_texture.texture_srv->GetDescriptorIndex();
        }

        if (material.emissive_index != -1)
        {
            material.emissive_index = m_textures[texture_offset + material.emissive_index].texture_srv->GetDescriptorIndex();
        }
        else
        {
            material.emissive_index = m_dummy_black_texture.texture_srv->GetDescriptorIndex();
        }

        if (material.normal_index != -1)
        {
            material.normal_index = m_textures[texture_offset + material.normal_index].texture_srv->GetDescriptorIndex();
        }
        else
        {
            material.normal_index = m_dummy_normal_texture.texture_srv->GetDescriptorIndex();
        }
    }

    std::vector<MeshRenderer> renderers;
    renderers.reserve(model.nodes.size());
    uint32_t bounding_offset = m_cull_data.size();
    for (const auto& node : model.nodes)
    {
        const auto& mesh = model.meshes[node.mesh_index];
        const auto& [vertex_buffer,
                     vertex_srv,
                     meshlet_buffer,
                     meshlet_srv,
                     meshlet_vertex_buffer,
                     meshlet_vertex_srv,
                     meshlet_tris_buffer,
                     meshlet_tris_srv] = mesh_buffers[node.mesh_index];
        const auto transform_index = static_cast<uint32_t>(m_transforms.size());
        auto final_transform = transform * model.transforms[node.transform_index];
        m_transforms.emplace_back(final_transform);
        const auto material_index = static_cast<int>(m_materials.size());
        m_materials.emplace_back(model.materials[mesh.material_index]);
        renderers.push_back({
            .m_vertex_buffer = vertex_buffer,
            .m_vertex_buffer_srv = vertex_srv,
            .m_mesh_buffer = meshlet_buffer,
            .m_mesh_buffer_srv = meshlet_srv,
            .m_mesh_vertex_buffer = meshlet_vertex_buffer,
            .m_mesh_vertex_buffer_srv = meshlet_vertex_srv,
            .m_mesh_triangle_buffer = meshlet_tris_buffer,
            .m_mesh_triangle_buffer_srv = meshlet_tris_srv,
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
    const auto& window = m_engine->GetWindow();
    auto window_size = window.GetSize();
    const auto float_size = std::array{ static_cast<float>(window_size[0]), static_cast<float>(window_size[1]) };
    command->SetViewport(Swift::Viewport{ .dimensions = float_size });
    command->SetScissor(Swift::Scissor{ .dimensions = { window_size.x, window_size.y } });
    auto* render_target = m_context->GetCurrentRenderTarget();
    command->BindRenderTargets(render_target, m_depth_stencil);
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
            .vertex_buffer = renderable.m_vertex_buffer_srv->GetDescriptorIndex(),
            .meshlet_buffer = renderable.m_mesh_buffer_srv->GetDescriptorIndex(),
            .mesh_vertex_buffer = renderable.m_mesh_vertex_buffer_srv->GetDescriptorIndex(),
            .mesh_triangle_buffer = renderable.m_mesh_triangle_buffer_srv->GetDescriptorIndex(),
            .material_index = renderable.m_material_index,
            .transform_index = renderable.m_transform_index,
            .meshlet_count = renderable.m_meshlet_count,
            .bounding_offset = renderable.m_bounding_offset,
            .ibl_index = m_specular_ibl_texture.texture_srv->GetDescriptorIndex(),
        };
        command->PushConstants(&push_constants, sizeof(PushConstants));
        renderable.Draw(command, true);
    }
}

void Renderer::DrawSkybox(Swift::ICommand* command) const
{
    const auto& window = m_engine->GetWindow();
    auto window_size = window.GetSize();
    const auto float_size = std::array{ static_cast<float>(window_size[0]), static_cast<float>(window_size[1]) };
    command->SetViewport(Swift::Viewport{ .dimensions = float_size });
    command->SetScissor(Swift::Scissor{ .dimensions = { window_size.x, window_size.y } });
    command->BindShader(m_skybox_shader);
    command->BindConstantBuffer(m_global_constant_buffer, 1);
    command->DispatchMesh(1, 1, 1);
}

void Renderer::DrawShadowPass(Swift::ICommand* command) const
{
    command->SetViewport(Swift::Viewport{ .dimensions = { 4096, 4096 } });
    command->SetScissor(Swift::Scissor{ .dimensions = { 4096, 4096 } });
    command->TransitionResource(m_shadow_texture->GetResource(), Swift::ResourceState::eDepthWrite);
    command->ClearDepthStencil(m_shadow_depth_stencil, 1.f, 0);
    command->BindRenderTargets(nullptr, m_shadow_depth_stencil);
    command->BindShader(m_shadow_shader);
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
            .vertex_buffer = renderable.m_vertex_buffer_srv->GetDescriptorIndex(),
            .meshlet_buffer = renderable.m_mesh_buffer_srv->GetDescriptorIndex(),
            .mesh_vertex_buffer = renderable.m_mesh_vertex_buffer_srv->GetDescriptorIndex(),
            .mesh_triangle_buffer = renderable.m_mesh_triangle_buffer_srv->GetDescriptorIndex(),
            .material_index = renderable.m_material_index,
            .transform_index = renderable.m_transform_index,
            .meshlet_count = renderable.m_meshlet_count,
            .bounding_offset = renderable.m_bounding_offset,
            .ibl_index = m_specular_ibl_texture.texture_srv->GetDescriptorIndex(),
        };
        command->PushConstants(&push_constants, sizeof(PushConstants));
        renderable.Draw(command, false);
    }
    command->TransitionResource(m_shadow_texture->GetResource(), Swift::ResourceState::eShaderResource);
}

void Renderer::InitImgui()
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
    init_info.SrvDescriptorFreeFn =
        [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle)
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
