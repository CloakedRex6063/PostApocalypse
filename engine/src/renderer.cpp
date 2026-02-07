#include "renderer.hpp"
#include "engine.hpp"
#include "shader_data.hpp"

Renderer::Renderer(Engine& engine) : m_engine(engine)
{
    const auto size = engine.GetWindow().GetSize();
    m_context = Swift::CreateContext({ .backend_type = Swift::BackendType::eD3D12,
                                       .width = size.x,
                                       .height = size.y,
                                       .native_window_handle = engine.GetWindow().GetNativeWindow(),
                                       .native_display_handle = nullptr });

    m_depth_texture = Swift::TextureBuilder(m_context,  size.x, size.y)
                          .SetFlags(EnumFlags(Swift::TextureFlags::eDepthStencil))
                          .SetFormat(Swift::Format::eD32F)
                          .Build();
    m_depth_stencil = m_context->CreateDepthStencil(m_depth_texture);

    m_global_constant_buffer = Swift::BufferBuilder(m_context, 65536).Build();

    std::vector<Swift::Descriptor> descriptors{};
    const auto descriptor = Swift::DescriptorBuilder(Swift::DescriptorType::eConstant).SetShaderRegister(1).Build();
    descriptors.emplace_back(descriptor);
    std::vector<Swift::SamplerDescriptor> samplers{{}};

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

Renderer::~Renderer()
{
    if (m_skybox)
    {
        m_context->DestroyTexture(m_skybox);
    }
    Swift::DestroyContext(m_context);
}

void Renderer::Update() const
{
    auto* render_target = m_context->GetCurrentRenderTarget();
    auto* command = m_context->GetCurrentCommand();

    auto& camera = m_engine.GetCamera();
    auto& window = m_engine.GetWindow();
    auto window_size = window.GetSize();
    const auto float_size = std::array{static_cast<float>(window_size[0]), static_cast<float>(window_size[1])};
    GlobalConstantInfo info
    {
        .view_proj = camera.m_proj_matrix * camera.m_view_matrix,
        .view = camera.m_view_matrix,
        .proj = camera.m_proj_matrix,
        .cubemap_index = m_skybox_srv->GetDescriptorIndex(),
    };
    m_global_constant_buffer->Write(&info, 0, sizeof(GlobalConstantInfo));

    command->Begin();

    command->SetViewport(Swift::Viewport{.dimensions = float_size});
    command->SetScissor(Swift::Scissor{.dimensions = {window_size.x, window_size.y}});

    command->TransitionResource(render_target->GetTexture()->GetResource(), Swift::ResourceState::eRenderTarget);
    command->ClearRenderTarget(render_target, { 0.392f, 0.584f, 0.929f, 1.0 });
    command->TransitionResource(m_depth_stencil->GetTexture()->GetResource(), Swift::ResourceState::eDepthWrite);
    command->ClearDepthStencil(m_depth_stencil, 1.f, 0);

    command->BindShader(m_skybox_shader);
    command->BindConstantBuffer(m_global_constant_buffer, 1);

    command->BindRenderTargets(render_target, m_depth_stencil);
    command->DispatchMesh(1, 1, 1);

    command->TransitionResource(render_target->GetTexture()->GetResource(), Swift::ResourceState::ePresent);
    command->End();
    m_context->Present(false);
}
