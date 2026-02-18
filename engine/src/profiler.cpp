#include "profiler.hpp"
#include "tracy/Tracy.hpp"

GPUProfiler::GPUProfiler(Swift::IContext* context) : m_context(context)
{
    auto* device = static_cast<ID3D12Device14*>(m_context->GetDevice());
    auto* queue = static_cast<ID3D12CommandQueue*>(m_context->GetGraphicsQueue()->GetQueue());
    m_tracy_context = TracyD3D12Context(device, queue);
}
GPUProfiler::~GPUProfiler() { TracyD3D12Destroy(m_tracy_context); }

void GPUProfiler::NewFrame() const
{
    m_tracy_context->Collect();
    m_tracy_context->NewFrame();
}

