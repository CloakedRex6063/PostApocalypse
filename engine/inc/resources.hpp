#pragma once

class Renderer;

struct Vertex
{
    glm::vec3 position;
    float uv_x;
    glm::vec3 normal;
    float uv_y;
    glm::vec4 tangent;
};

struct Mesh
{
    std::string name;
    std::vector<meshopt_Meshlet> meshlets;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> meshlet_vertices;
    std::vector<uint32_t> meshlet_triangles;
    int material_index;
};

struct Material
{
    glm::vec4 albedo;

    glm::vec3 emissive;
    int albedo_index;

    int emissive_index;
    int metal_rough_index;
    float metallic;
    float roughness;

    int normal_index;
    int occlusion_index;
    float alpha_cutoff;
    Swift::AlphaMode alpha_mode;
};

struct Texture
{
    std::string name;
    uint32_t sampler_index = 0;
    uint32_t width = 1;
    uint32_t height = 1;
    uint16_t mip_levels = 1;
    uint16_t array_size = 1;
    Swift::Format format;
    std::vector<uint8_t> pixels;
};

struct Sampler
{
    std::string name;
    Swift::Filter min_filter;
    Swift::Filter mag_filter;
    Swift::Wrap wrap_u;
    Swift::Wrap wrap_y;
};

struct Node
{
    std::string name;
    uint32_t transform_index;
    int mesh_index;
};

struct CullData
{
    glm::vec3 center{};
    float radius{};
    glm::vec3 cone_apex;
    uint32_t cone_packed;
};

struct Model
{
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<Texture> textures;
    std::vector<Sampler> samplers;
    std::vector<glm::mat4> transforms;
    std::vector<Node> nodes;
    std::vector<CullData> cull_datas;
};

class Engine;
class Actor;

class Resources
{
public:
    Resources(Engine* engine);
    ~Resources();

    std::shared_ptr<Actor> LoadModel(const std::filesystem::path& path);
    Swift::ITexture* LoadTexture(const std::filesystem::path& path) const;

private:
    static std::tuple<std::vector<meshopt_Meshlet>, std::vector<uint32_t>, std::vector<uint8_t>> BuildMeshlets(
        std::span<const Vertex> vertices,
        std::span<const uint32_t> indices);

    static std::vector<uint32_t> RepackMeshlets(std::span<meshopt_Meshlet> meshlets,
                                                std::span<const uint8_t> meshlet_triangles);

    static void LoadTangents(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
    static Texture LoadTexture(const fastgltf::Asset& asset, const fastgltf::Texture& texture);
    static uint32_t PackCone(const meshopt_Bounds& bounds);

    static Swift::Filter ToFilter(std::optional<fastgltf::Filter> filter);
    static Swift::Wrap ToWrap(fastgltf::Wrap wrap);

    void LoadNode(const fastgltf::Asset& asset,
                  size_t node_index,
                  const glm::mat4& parent_transform,
                  std::vector<Node>& nodes,
                  std::vector<glm::mat4>& transforms,
                  const std::vector<std::pair<uint32_t, uint32_t>>& mesh_ranges);
    std::tuple<std::vector<Node>, std::vector<glm::mat4>> LoadNodes(
        const fastgltf::Asset& asset,
        const std::vector<std::pair<uint32_t, uint32_t>>& mesh_ranges);
    static std::vector<uint32_t> LoadIndices(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive);
    static std::vector<Vertex> LoadVertices(const fastgltf::Asset& asset,
                                     const fastgltf::Primitive& primitive,
                                     std::vector<uint32_t>& indices);
    static glm::mat4 GetLocalTransform(const fastgltf::Node& node);
    std::vector<Mesh> LoadMesh(Model& model, const fastgltf::Asset& asset, const fastgltf::Mesh& mesh);
    static Material LoadMaterial(const fastgltf::Material& material);
    static Sampler LoadSampler(const fastgltf::Sampler& sampler);

    Engine* m_engine;
};