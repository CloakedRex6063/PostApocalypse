#pragma once
#define TRACY_CALLSTACK 8
#include "tracy/TracyD3D12.hpp"

class GPUProfiler
{
public:
    GPUProfiler(Swift::IContext* context);
    ~GPUProfiler();

    void NewFrame() const;
    TracyD3D12Ctx GetTracyContext() const { return m_tracy_context; }

private:
    Swift::IContext* m_context;
    TracyD3D12Ctx m_tracy_context;
};

#define GPU_ZONE(profiler, cmd_list, name) \
    TracyD3D12Zone((profiler)->GetTracyContext(), static_cast<ID3D12GraphicsCommandList*>(cmd_list->GetCommandList()), name)

#define CPU_ZONE(name) ZoneScopedN(name)