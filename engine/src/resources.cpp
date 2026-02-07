#include "resources.hpp"
#include "dds.h"
#include "renderer.hpp"

Resources::Resources(Renderer &renderer) : m_renderer(renderer)
{
}

Resources::~Resources()
{
}

void Resources::LoadModel(const std::filesystem::path &path)
{
}

constexpr Swift::Format FromDXGIFormat(const dds::DXGI_FORMAT format) noexcept
{
    switch (format)
    {
        case dds::DXGI_FORMAT_R8G8B8A8_UNORM:
            return Swift::Format::eRGBA8_UNORM;
        case dds::DXGI_FORMAT_R16G16B16A16_FLOAT:
            return Swift::Format::eRGBA16F;
        case dds::DXGI_FORMAT_R32G32B32A32_FLOAT:
            return Swift::Format::eRGBA32F;
        case dds::DXGI_FORMAT_D32_FLOAT:
        case dds::DXGI_FORMAT_R32_FLOAT:
        case dds::DXGI_FORMAT_R32_TYPELESS:
            return Swift::Format::eD32F;
        case dds::DXGI_FORMAT_BC1_UNORM:
            return Swift::Format::eBC1_UNORM;
        case dds::DXGI_FORMAT_BC1_UNORM_SRGB:
            return Swift::Format::eBC1_UNORM_SRGB;
        case dds::DXGI_FORMAT_BC2_UNORM:
            return Swift::Format::eBC2_UNORM;
        case dds::DXGI_FORMAT_BC2_UNORM_SRGB:
            return Swift::Format::eBC2_UNORM_SRGB;
        case dds::DXGI_FORMAT_BC3_UNORM:
            return Swift::Format::eBC3_UNORM;
        case dds::DXGI_FORMAT_BC3_UNORM_SRGB:
            return Swift::Format::eBC3_UNORM_SRGB;
        case dds::DXGI_FORMAT_BC4_UNORM:
            return Swift::Format::eBC4_UNORM;
        case dds::DXGI_FORMAT_BC4_SNORM:
            return Swift::Format::eBC4_SNORM;
        case dds::DXGI_FORMAT_BC5_UNORM:
            return Swift::Format::eBC5_UNORM;
        case dds::DXGI_FORMAT_BC5_SNORM:
            return Swift::Format::eBC5_SNORM;
        case dds::DXGI_FORMAT_BC6H_UF16:
            return Swift::Format::eBC6H_UF16;
        case dds::DXGI_FORMAT_BC6H_SF16:
            return Swift::Format::eBC6H_SF16;
        case dds::DXGI_FORMAT_BC7_UNORM:
            return Swift::Format::eBC7_UNORM;
        case dds::DXGI_FORMAT_BC7_UNORM_SRGB:
            return Swift::Format::eBC7_UNORM_SRGB;
        default:
            return Swift::Format::eRGBA8_UNORM;
    }
}

Swift::ITexture *Resources::LoadTexture(const std::filesystem::path &path) const
{
    std::ifstream stream;
    stream.open(path, std::ios::binary);
    std::vector<char> header_data;
    header_data.resize(sizeof(dds::Header));
    stream.read(header_data.data(), sizeof(dds::Header));
    const auto header = dds::read_header(header_data.data(), sizeof(dds::Header));
    stream.seekg(header.data_offset(), std::ios::beg);
    std::vector<uint8_t> data;
    data.resize(header.data_size());
    stream.read(reinterpret_cast<char *>(data.data()), header.data_size());
    return Swift::TextureBuilder(m_renderer.GetContext(), header.width(), header.height()).SetArraySize(
                header.array_size()).
            SetMipmapLevels(header.mip_levels()).SetFormat(FromDXGIFormat(header.format())).SetData(data.data()).
            Build();
}
