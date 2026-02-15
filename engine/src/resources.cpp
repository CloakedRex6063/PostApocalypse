#include "resources.hpp"
#include "dds.h"
#include "renderer.hpp"
#include "mikktspace.h"
#define STB_IMAGE_IMPLEMENTATION
#include "engine.hpp"
#include "stb_image.h"

Resources::Resources(Engine* engine) : m_engine(engine) {}

Resources::~Resources() {}

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

Swift::ITexture* Resources::LoadTexture(const std::filesystem::path& path) const
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
    stream.read(reinterpret_cast<char*>(data.data()), header.data_size());
    return Swift::TextureBuilder(m_engine->GetRenderer().GetContext(), header.width(), header.height())
        .SetArraySize(header.array_size())
        .SetMipmapLevels(header.mip_levels())
        .SetFormat(FromDXGIFormat(header.format()))
        .SetData(data.data())
        .Build();
}

std::tuple<std::vector<meshopt_Meshlet>, std::vector<uint32_t>, std::vector<uint8_t>> Resources::BuildMeshlets(
    const std::span<const Vertex> vertices,
    const std::span<const uint32_t> indices)
{
    std::vector<meshopt_Meshlet> meshlets;
    std::vector<uint32_t> mesh_vertices;
    std::vector<uint8_t> mesh_triangles;
    const auto max_meshlets = meshopt_buildMeshletsBound(indices.size(), 64, 124);
    meshlets.resize(max_meshlets);
    mesh_vertices.resize(max_meshlets * 64);
    mesh_triangles.resize(max_meshlets * 124 * 3);
    const auto meshlet_count = meshopt_buildMeshlets(meshlets.data(),
                                                     mesh_vertices.data(),
                                                     mesh_triangles.data(),
                                                     indices.data(),
                                                     indices.size(),
                                                     reinterpret_cast<const float*>(vertices.data()),
                                                     vertices.size(),
                                                     sizeof(Vertex),
                                                     64,
                                                     124,
                                                     0.f);
    const auto& [vertex_offset, triangle_offset, vertex_count, triangle_count] = meshlets[meshlet_count - 1];
    mesh_vertices.resize(vertex_offset + vertex_count);
    mesh_triangles.resize(triangle_offset + (triangle_count * 3 + 3 & ~3));
    meshlets.resize(meshlet_count);
    return { meshlets, mesh_vertices, mesh_triangles };
}

std::vector<uint32_t> Resources::RepackMeshlets(std::span<meshopt_Meshlet> meshlets,
                                                const std::span<const uint8_t> meshlet_triangles)
{
    std::vector<uint32_t> repacked_meshlets;
    for (auto& m : meshlets)
    {
        const auto triangle_offset = static_cast<uint32_t>(repacked_meshlets.size());

        for (uint32_t i = 0; i < m.triangle_count; ++i)
        {
            const auto idx0 = meshlet_triangles[m.triangle_offset + i * 3 + 0];
            const auto idx1 = meshlet_triangles[m.triangle_offset + i * 3 + 1];
            const auto idx2 = meshlet_triangles[m.triangle_offset + i * 3 + 2];
            auto packed = (static_cast<uint32_t>(idx0) & 0xFF) << 0 | (static_cast<uint32_t>(idx1) & 0xFF) << 8 |
                          (static_cast<uint32_t>(idx2) & 0xFF) << 16;
            repacked_meshlets.push_back(packed);
        }

        m.triangle_offset = triangle_offset;
    }
    return repacked_meshlets;
}

void Resources::LoadTangents(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
{
    struct Pair
    {
        std::span<Vertex> vertices;
        std::span<uint32_t> indices;
    } pair{
        .vertices = vertices,
        .indices = indices,
    };
    SMikkTSpaceInterface space_interface{
        .m_getNumFaces = [](const SMikkTSpaceContext* ctx) -> int
        { return static_cast<Pair*>(ctx->m_pUserData)->indices.size() / 3; },
        .m_getNumVerticesOfFace = [](const SMikkTSpaceContext*, int) -> int { return 3; },
        .m_getPosition =
            [](const SMikkTSpaceContext* ctx, float out[], int faceIdx, int vertIdx)
        {
            const auto pair = static_cast<Pair*>(ctx->m_pUserData);
            int idx = pair->indices[faceIdx * 3 + vertIdx];
            out[0] = pair->vertices[idx].position[0];
            out[1] = pair->vertices[idx].position[1];
            out[2] = pair->vertices[idx].position[2];
        },
        .m_getNormal =
            [](const SMikkTSpaceContext* ctx, float out[], int faceIdx, int vertIdx)
        {
            const auto pair = static_cast<Pair*>(ctx->m_pUserData);
            int idx = pair->indices[faceIdx * 3 + vertIdx];
            out[0] = pair->vertices[idx].normal[0];
            out[1] = pair->vertices[idx].normal[1];
            out[2] = pair->vertices[idx].normal[2];
        },
        .m_getTexCoord =
            [](const SMikkTSpaceContext* ctx, float out[], int faceIdx, int vertIdx)
        {
            const auto pair = static_cast<Pair*>(ctx->m_pUserData);
            int idx = pair->indices[faceIdx * 3 + vertIdx];
            out[0] = pair->vertices[idx].uv_x;
            out[1] = pair->vertices[idx].uv_y;
        },
        .m_setTSpaceBasic = static_cast<decltype(SMikkTSpaceInterface::m_setTSpaceBasic)>(
            [](const SMikkTSpaceContext* ctx, const float tangent[], const float sign, const int faceIdx, const int vertIdx)
            {
                const auto pair = static_cast<Pair*>(ctx->m_pUserData);
                const int idx = pair->indices[faceIdx * 3 + vertIdx];
                pair->vertices[idx].tangent = { tangent[0], tangent[1], tangent[2], sign };
            })
    };

    const SMikkTSpaceContext context{
        .m_pInterface = &space_interface,
        .m_pUserData = &pair,
    };
    genTangSpaceDefault(&context);
}

Texture Resources::LoadTexture(const fastgltf::Asset& asset, const fastgltf::Texture& texture)
{
    const bool isDDS = texture.ddsImageIndex.has_value();
    const auto& image = isDDS ? asset.images[texture.ddsImageIndex.value()] : asset.images[texture.imageIndex.value()];

    // Get image data
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
    uint16_t mip_levels = 1;
    uint16_t array_size = 1;
    auto format = Swift::Format::eRGBA8_UNORM;

    std::visit(
        fastgltf::visitor{ [&](const fastgltf::sources::BufferView& view)
                           {
                               const auto& bufferView = asset.bufferViews[view.bufferViewIndex];
                               const auto& buffer = asset.buffers[bufferView.bufferIndex];

                               std::visit(
                                   fastgltf::visitor{ [&](const fastgltf::sources::Array& array)
                                                      {
                                                          const auto* dataPtr = reinterpret_cast<const unsigned char*>(
                                                              array.bytes.data() + bufferView.byteOffset);
                                                          const auto dataSize = static_cast<int>(bufferView.byteLength);
                                                          int w, h, channels;
                                                          unsigned char* data =
                                                              stbi_load_from_memory(dataPtr, dataSize, &w, &h, &channels, 4);
                                                          width = static_cast<uint32_t>(w);
                                                          height = static_cast<uint32_t>(h);
                                                          pixels.assign(data, data + (w * h * 4));
                                                          stbi_image_free(data);
                                                      },
                                                      [](auto&&) {} },
                                   buffer.data);
                           },
                           [&](const fastgltf::sources::Array& array)
                           {
                               const auto* dataPtr = reinterpret_cast<const unsigned char*>(array.bytes.data());
                               const auto dataSize = static_cast<int>(array.bytes.size());
                               if (isDDS)
                               {
                                   const auto header = dds::read_header(array.bytes.data(), sizeof(dds::Header));
                                   pixels.resize(header.data_size());
                                   const auto* start =
                                       reinterpret_cast<const uint8_t*>(array.bytes.data() + header.data_offset());
                                   pixels.assign(start, start + header.data_size());

                                   width = header.width();
                                   height = header.height();
                                   mip_levels = header.mip_levels();
                                   array_size = header.array_size();
                                   format = FromDXGIFormat(header.format());
                               }
                               else
                               {
                                   int w = 0, h = 0, channels = 0;
                                   unsigned char* data = stbi_load_from_memory(dataPtr, dataSize, &w, &h, &channels, 4);
                                   width = static_cast<uint32_t>(w);
                                   height = static_cast<uint32_t>(h);
                                   pixels.assign(data, data + (w * h * 4));
                                   stbi_image_free(data);
                               }
                           },
                           [&](const fastgltf::sources::URI& uri)
                           {
                               const std::string path = uri.uri.fspath().string();
                               std::string ext = uri.uri.fspath().extension().string();
                               std::ranges::transform(ext, ext.begin(), ::tolower);

                               if (isDDS)
                               {
                                   std::ifstream stream;
                                   stream.open(path, std::ios::binary);
                                   std::vector<char> header_data;
                                   header_data.resize(sizeof(dds::Header));
                                   stream.read(header_data.data(), sizeof(dds::Header));
                                   const auto header = dds::read_header(header_data.data(), sizeof(dds::Header));
                                   stream.seekg(header.data_offset(), std::ios::beg);
                                   pixels.resize(header.data_size());
                                   stream.read(reinterpret_cast<char*>(pixels.data()), header.data_size());

                                   width = header.width();
                                   height = header.height();
                                   mip_levels = header.mip_levels();
                                   array_size = header.array_size();
                               }
                               else
                               {
                                   // Load with stb_image
                                   int w, h, channels;
                                   unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
                                   width = static_cast<uint32_t>(w);
                                   height = static_cast<uint32_t>(h);
                                   if (data)
                                   {
                                       pixels.assign(data, data + (w * h * 4));
                                       stbi_image_free(data);
                                   }
                               }
                           },
                           [](auto&&) {} },
        image.data);

    Texture t{
        .name = std::string(image.name),
        .width = width,
        .height = height,
        .mip_levels = mip_levels,
        .array_size = array_size,
        .format = format,
        .pixels = pixels,
    };
    return t;
}

glm::mat4 Resources::GetLocalTransform(const fastgltf::Node& node)
{
    return std::visit(
        fastgltf::visitor{
            [](const fastgltf::TRS& trs)
            {
                auto T = glm::translate(glm::mat4(1.0f), glm::vec3(trs.translation[0], trs.translation[1], trs.translation[2]));
                auto R = glm::mat4_cast(glm::quat(trs.rotation[3], trs.rotation[0], trs.rotation[1], trs.rotation[2]));
                auto S = glm::scale(glm::mat4(1.0f), glm::vec3(trs.scale[0], trs.scale[1], trs.scale[2]));
                return T * R * S;
            },
            [](const fastgltf::math::fmat4x4& matrix) { return glm::make_mat4(matrix.data()); } },
        node.transform);
}

uint32_t Resources::PackCone(const meshopt_Bounds& bounds)
{
    return (uint32_t(uint8_t(bounds.cone_axis_s8[0])) << 0) | (uint32_t(uint8_t(bounds.cone_axis_s8[1])) << 8) |
           (uint32_t(uint8_t(bounds.cone_axis_s8[2])) << 16) | (uint32_t(uint8_t(bounds.cone_cutoff_s8)) << 24);
}

std::vector<Mesh> Resources::LoadMesh(Model& model, const fastgltf::Asset& asset, const fastgltf::Mesh& mesh)
{
    std::vector<Mesh> meshes;

    for (const auto& prim : mesh.primitives)
    {
        auto indices = LoadIndices(asset, prim);
        const auto vertices = LoadVertices(asset, prim, indices);
        auto [meshlets, meshlet_vertices, meshlet_triangles] = BuildMeshlets(vertices, indices);

        for (const auto& meshlet : meshlets)
        {
            const meshopt_Bounds bounds = meshopt_computeMeshletBounds(&meshlet_vertices[meshlet.vertex_offset],
                                                                       &meshlet_triangles[meshlet.triangle_offset],
                                                                       meshlet.triangle_count,
                                                                       &vertices[0].position.x,
                                                                       vertices.size(),
                                                                       sizeof(Vertex));

            model.cull_datas.push_back(CullData{
                .center = glm::vec3(bounds.center[0], bounds.center[1], bounds.center[2]),
                .radius = bounds.radius,
                .cone_apex = glm::vec3(bounds.cone_apex[0], bounds.cone_apex[1], bounds.cone_apex[2]),
                .cone_packed = PackCone(bounds),
            });
        }

        const auto repacked_triangles = RepackMeshlets(meshlets, meshlet_triangles);
        Mesh m{
            .name = std::string(mesh.name),
            .meshlets = meshlets,
            .vertices = vertices,
            .meshlet_vertices = meshlet_vertices,
            .meshlet_triangles = repacked_triangles,
            .material_index = static_cast<int>(prim.materialIndex.value_or(-1)),
        };
        meshes.emplace_back(m);
    }
    return meshes;
}

Material Resources::LoadMaterial(const fastgltf::Material& material)
{
    const glm::vec4 albedo = glm::make_vec4(material.pbrData.baseColorFactor.data());

    const glm::vec3 emissive = glm::make_vec3(material.emissiveFactor.data());

    auto alpha_mode = Swift::AlphaMode::eOpaque;
    if (material.alphaMode == fastgltf::AlphaMode::Blend)
    {
        alpha_mode = Swift::AlphaMode::eTransparent;
    }

    const int albedo_index =
        material.pbrData.baseColorTexture.has_value() ? static_cast<int>(material.pbrData.baseColorTexture->textureIndex) : -1;

    const int emissive_index =
        material.emissiveTexture.has_value() ? static_cast<int>(material.emissiveTexture->textureIndex) : -1;

    const int metal_rough_index = material.pbrData.metallicRoughnessTexture.has_value()
                                      ? static_cast<int>(material.pbrData.metallicRoughnessTexture->textureIndex)
                                      : -1;

    const int normal_index = material.normalTexture.has_value() ? static_cast<int>(material.normalTexture->textureIndex) : -1;

    const int occlusion_index =
        material.occlusionTexture.has_value() ? static_cast<int>(material.occlusionTexture->textureIndex) : -1;

    const Material m{
        .albedo = albedo,
        .emissive = emissive,
        .albedo_index = albedo_index,
        .emissive_index = emissive_index,
        .metal_rough_index = metal_rough_index,
        .metallic = material.pbrData.metallicFactor,
        .roughness = material.pbrData.roughnessFactor,
        .normal_index = normal_index,
        .occlusion_index = occlusion_index,
        .alpha_cutoff = material.alphaCutoff,
        .alpha_mode = alpha_mode,
    };
    return m;
}

Sampler Resources::LoadSampler(const fastgltf::Sampler& sampler)
{
    Sampler s{
        .name = std::string(sampler.name),
        .min_filter = ToFilter(sampler.minFilter),
        .mag_filter = ToFilter(sampler.magFilter),
        .wrap_u = ToWrap(sampler.wrapS),
        .wrap_y = ToWrap(sampler.wrapT),
    };
    return s;
}

std::shared_ptr<Actor> Resources::LoadModel(const std::filesystem::path& path, const glm::vec3 scale)
{
    constexpr auto extensions = fastgltf::Extensions::KHR_materials_transmission | fastgltf::Extensions::KHR_materials_volume |
                                fastgltf::Extensions::KHR_materials_specular |
                                fastgltf::Extensions::KHR_materials_emissive_strength |
                                fastgltf::Extensions::KHR_materials_ior | fastgltf::Extensions::KHR_texture_transform |
                                fastgltf::Extensions::KHR_materials_unlit | fastgltf::Extensions::MSFT_texture_dds;
    fastgltf::Parser parser{ extensions };

    auto data = fastgltf::GltfDataBuffer::FromPath(path);
    if (data.error() != fastgltf::Error::None)
    {
        printf("Failed to load glTF: %s\n", fastgltf::getErrorMessage(data.error()).data());
        return nullptr;
    }

    constexpr auto gltfOptions = fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages;

    auto asset = parser.loadGltf(data.get(), std::filesystem::path(path).parent_path(), gltfOptions);
    if (asset.error() != fastgltf::Error::None)
    {
        printf("Failed to parse glTF: %s\n", fastgltf::getErrorMessage(asset.error()).data());
        return nullptr;
    }

    Model m{};

    std::vector<std::pair<uint32_t, uint32_t>> mesh_ranges;

    for (auto& mesh : asset->meshes)
    {
        uint32_t start = static_cast<uint32_t>(m.meshes.size());
        auto meshes = LoadMesh(m, asset.get(), mesh);
        m.meshes.insert(m.meshes.end(), meshes.begin(), meshes.end());
        mesh_ranges.push_back({ start, static_cast<uint32_t>(meshes.size()) });
    }

    for (auto& texture : asset->textures)
    {
        auto tex = LoadTexture(asset.get(), texture);
        m.textures.emplace_back(tex);
    }

    for (auto& material : asset->materials)
    {
        auto mat = LoadMaterial(material);
        m.materials.emplace_back(mat);
    }

    for (auto& sampler : asset->samplers)
    {
        auto samp = LoadSampler(sampler);
        m.samplers.emplace_back(samp);
    }

    std::tie(m.nodes, m.transforms) = LoadNodes(asset.get(), mesh_ranges, scale);

    auto actor = m_engine->GetScene().AddActor<Actor>();
    actor->AddModel(m);
    return actor;
}

std::vector<Vertex> Resources::LoadVertices(const fastgltf::Asset& asset,
                                            const fastgltf::Primitive& primitive,
                                            std::vector<uint32_t>& indices)
{
    const auto positionIt = primitive.findAttribute("POSITION");
    const auto normalIt = primitive.findAttribute("NORMAL");
    const auto uvIt = primitive.findAttribute("TEXCOORD_0");

    if (positionIt == primitive.attributes.end()) return {};

    auto& positionAccessor = asset.accessors[primitive.findAttribute("POSITION")->accessorIndex];
    std::vector<Vertex> vertices(positionAccessor.count);
    fastgltf::iterateAccessorWithIndex<glm::vec3>(asset,
                                                  positionAccessor,
                                                  [&](const glm::vec3& position, const size_t index)
                                                  { vertices[index].position = position; });

    auto& normalAccessor = asset.accessors[normalIt->accessorIndex];
    fastgltf::iterateAccessorWithIndex<glm::vec3>(asset,
                                                  normalAccessor,
                                                  [&](const glm::vec3& normal, const size_t index)
                                                  { vertices[index].normal = normal; });

    auto& uvAccessor = asset.accessors[uvIt->accessorIndex];
    fastgltf::iterateAccessorWithIndex<glm::vec2>(asset,
                                                  uvAccessor,
                                                  [&](const glm::vec2& uv, const size_t index)
                                                  {
                                                      vertices[index].uv_x = uv.x;
                                                      vertices[index].uv_y = uv.y;
                                                  });

    LoadTangents(vertices, indices);
    return vertices;
}

std::vector<uint32_t> Resources::LoadIndices(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive)
{
    std::vector<uint32_t> indices;

    if (!primitive.indicesAccessor.has_value()) return indices;

    const auto& accessor = asset.accessors[primitive.indicesAccessor.value()];
    indices.resize(accessor.count);

    switch (accessor.componentType)
    {
        case fastgltf::ComponentType::UnsignedShort: {
            std::vector<uint16_t> shortIndices(accessor.count);
            fastgltf::copyFromAccessor<uint16_t>(asset, accessor, shortIndices.data());
            std::ranges::copy(shortIndices, indices.begin());
            break;
        }
        case fastgltf::ComponentType::UnsignedInt: {
            fastgltf::copyFromAccessor<uint32_t>(asset, accessor, indices.data());
            break;
        }
        default:
            break;
    }

    return indices;
}

std::tuple<std::vector<Node>, std::vector<glm::mat4>> Resources::LoadNodes(
    const fastgltf::Asset& asset,
    const std::vector<std::pair<uint32_t, uint32_t>>& mesh_ranges,
    const glm::vec3 scale)
{
    std::vector<Node> nodes;
    std::vector<glm::mat4> transforms;

    const auto& scene = asset.scenes[asset.defaultScene.value_or(0)];
    for (const auto& nodeIndex : scene.nodeIndices)
    {
        LoadNode(asset, nodeIndex, glm::scale(glm::mat4(1.0f), scale), nodes, transforms, mesh_ranges);
    }

    return { nodes, transforms };
}

void Resources::LoadNode(const fastgltf::Asset& asset,
                         const size_t node_index,
                         const glm::mat4& parent_transform,
                         std::vector<Node>& nodes,
                         std::vector<glm::mat4>& transforms,
                         const std::vector<std::pair<uint32_t, uint32_t>>& mesh_ranges)
{
    const fastgltf::Node& node = asset.nodes[node_index];

    glm::mat4 world_transform = parent_transform * GetLocalTransform(node);

    const auto transform_idx = static_cast<uint32_t>(transforms.size());
    transforms.push_back(world_transform);

    if (node.meshIndex.has_value())
    {
        auto [start, count] = mesh_ranges[node.meshIndex.value()];
        for (uint32_t i = 0; i < count; ++i)
        {
            nodes.push_back(Node{
                .name = std::string(node.name),
                .transform_index = transform_idx,
                .mesh_index = static_cast<int>(start + i),
            });
        }
    }

    for (const size_t child : node.children)
    {
        LoadNode(asset, child, world_transform, nodes, transforms, mesh_ranges);
    }
}

Swift::Filter Resources::ToFilter(std::optional<fastgltf::Filter> filter)
{
    if (!filter.has_value()) return Swift::Filter::eLinear;

    switch (filter.value())
    {
        case fastgltf::Filter::Nearest:
            return Swift::Filter::eNearest;
        case fastgltf::Filter::Linear:
            return Swift::Filter::eLinear;
        case fastgltf::Filter::NearestMipMapNearest:
            return Swift::Filter::eNearestMipNearest;
        case fastgltf::Filter::LinearMipMapNearest:
            return Swift::Filter::eLinearMipNearest;
        case fastgltf::Filter::NearestMipMapLinear:
            return Swift::Filter::eNearestMipLinear;
        case fastgltf::Filter::LinearMipMapLinear:
            return Swift::Filter::eLinearMipLinear;
    }
    return Swift::Filter::eNearest;
}

Swift::Wrap Resources::ToWrap(fastgltf::Wrap wrap)
{
    switch (wrap)
    {
        case fastgltf::Wrap::Repeat:
            return Swift::Wrap::eRepeat;
        case fastgltf::Wrap::ClampToEdge:
            return Swift::Wrap::eClampToEdge;
        case fastgltf::Wrap::MirroredRepeat:
            return Swift::Wrap::eMirroredRepeat;
    }
    return Swift::Wrap::eRepeat;
}
