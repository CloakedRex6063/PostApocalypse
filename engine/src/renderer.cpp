#include "renderer.hpp"
#include "engine.hpp"
#include "shader_data.hpp"

Renderer::Renderer(Engine* engine) : m_engine(engine)
{
    InitContext();

    InitBuffers();
    InitPBRShader();
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
    if (m_skybox_texture.texture)
    {
        m_context->DestroyTexture(m_skybox_texture.texture);
        m_context->DestroyShaderResource(m_skybox_texture.texture_srv);
    }
    Swift::DestroyContext(m_context);
}

void Renderer::Update() const
{
    auto* render_target = m_context->GetCurrentRenderTarget();
    auto* command = m_context->GetCurrentCommand();

    auto& camera = m_engine->GetCamera();
    auto& window = m_engine->GetWindow();
    auto window_size = window.GetSize();
    const auto float_size = std::array{ static_cast<float>(window_size[0]), static_cast<float>(window_size[1]) };
    const GlobalConstantInfo info{
        .view_proj = camera.m_proj_matrix * camera.m_view_matrix,
        .view = camera.m_view_matrix,
        .proj = camera.m_proj_matrix,
        .cam_pos = camera.m_position,
        .cubemap_index = m_skybox_texture.texture_srv->GetDescriptorIndex(),
        .transform_buffer_index = m_transform_buffer_srv->GetDescriptorIndex(),
        .material_buffer_index = m_material_buffer_srv->GetDescriptorIndex(),
        .point_light_buffer_index = m_point_light_buffer_srv->GetDescriptorIndex(),
        .dir_light_buffer_index = m_dir_light_buffer_srv->GetDescriptorIndex(),
        .point_light_count = static_cast<uint32_t>(m_point_lights.size()),
        .dir_light_count = static_cast<uint32_t>(m_dir_lights.size()),
    };
    m_global_constant_buffer->Write(&info, 0, sizeof(GlobalConstantInfo));

    command->Begin();

    command->SetViewport(Swift::Viewport{ .dimensions = float_size });
    command->SetScissor(Swift::Scissor{ .dimensions = { window_size.x, window_size.y } });

    command->TransitionResource(render_target->GetTexture()->GetResource(), Swift::ResourceState::eRenderTarget);
    command->ClearRenderTarget(render_target, { 0.392f, 0.584f, 0.929f, 1.0 });
    command->TransitionResource(m_depth_stencil->GetTexture()->GetResource(), Swift::ResourceState::eDepthWrite);
    command->ClearDepthStencil(m_depth_stencil, 1.f, 0);

    command->BindRenderTargets(render_target, m_depth_stencil);
    command->BindShader(m_skybox_shader);
    command->BindConstantBuffer(m_global_constant_buffer, 1);

    command->DispatchMesh(1, 1, 1);

    command->BindShader(m_pbr_shader);
    for (auto& renderable : m_renderables)
    {
        renderable.Draw(command, false);
    }

    command->TransitionResource(render_target->GetTexture()->GetResource(), Swift::ResourceState::ePresent);
    command->End();
    m_context->Present(false);
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
        Swift::HeapCreateInfo{ .type = Swift::HeapType::eGPU_Upload, .size = 1'000'000'000, .debug_name = "Texture Heap" });
}

void Renderer::InitBuffers()
{
    m_global_constant_buffer = Swift::BufferBuilder(m_context, 65536).Build();
    m_transform_buffer = Swift::BufferBuilder(m_context, 1'000'000 * sizeof(glm::mat4)).Build();
    m_transform_buffer_srv = m_context->CreateShaderResource(m_transform_buffer,
                                                             Swift::BufferSRVCreateInfo{
                                                                 .num_elements = 1'000'000,
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
    const std::vector<Swift::SamplerDescriptor> samplers{ {} };

    m_pbr_shader = Swift::GraphicsShaderBuilder(m_context)
                       .SetRTVFormats({ Swift::Format::eRGBA8_UNORM })
                       .SetDSVFormat(Swift::Format::eD32F)
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
    uint32_t bounding_offset = 0;
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

    auto offset = static_cast<uint32_t>(m_renderables.size());
    auto size = renderers.size();
    m_renderables.insert(m_renderables.end(), renderers.begin(), renderers.end());
    return { offset, size };
}
