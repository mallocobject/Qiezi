#include "obj_vertices.h"

#include "tinyobjloader/tiny_obj_loader.h"
#include <bit>
#include <cstdint>
#include <format>
#include <unordered_map>

namespace qiezi {
namespace {

// 按位置去重用的哈希。浮点直接用 == 比较即可：
// 同一个位置在 OBJ 里就是同一串文本，解析出来位模式相同。
struct PositionHash {
    std::size_t operator()(const glm::vec3 &p) const noexcept {
        const auto h = [](float f) {
            // -0.f 和 0.f 视为同一个位置
            const std::uint32_t bits = std::bit_cast<std::uint32_t>(f);
            return std::hash<std::uint32_t>{}(bits == 0x80000000u ? 0u : bits);
        };
        return (h(p.x) << 0) ^ (h(p.y) << 10) ^ (h(p.z) << 20);
    }
};

struct PositionEqual {
    bool operator()(const glm::vec3 &a, const glm::vec3 &b) const noexcept {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }
};

} // namespace

std::optional<Mesh> load_obj(std::string_view path, std::string *out_error) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    // LoadObj 要 const char *，string_view 不保证以 '\0' 结尾，必须先转成
    // string。
    const std::string filename(path);

    if (!tinyobj::LoadObj(&attrib,
                          &shapes,
                          &materials,
                          &warn,
                          &err,
                          filename.c_str(),
                          /*mtl_basedir=*/nullptr,
                          /*triangulate=*/true)) {
        if (out_error != nullptr) {
            *out_error =
                err.empty() ? std::format("failed to load obj: {}", path) : err;
        }
        return std::nullopt;
    }

    // attrib.vertices 是扁平的 [x,y,z, x,y,z, ...]，v 下标 n 对应 3n..3n+2。
    const std::size_t vertex_count = attrib.vertices.size() / 3;

    Mesh mesh;
    mesh.vertices.reserve(vertex_count);
    // OBJ 里的 v 下标 -> mesh.vertices 的下标。先按原样填，遇到重复位置再改写。
    std::vector<unsigned int> remap(vertex_count);
    std::unordered_map<glm::vec3, unsigned int, PositionHash, PositionEqual>
        unique;
    unique.reserve(vertex_count);

    const auto intern = [&](int vertex_index) -> std::optional<unsigned int> {
        if (vertex_index < 0 ||
            static_cast<std::size_t>(vertex_index) >= vertex_count) {
            return std::nullopt;
        }
        const std::size_t base = 3 * static_cast<std::size_t>(vertex_index);
        const glm::vec3 p(attrib.vertices[base + 0],
                          attrib.vertices[base + 1],
                          attrib.vertices[base + 2]);

        const auto it = unique.find(p);
        if (it != unique.end()) {
            return it->second;
        }
        const unsigned int id = static_cast<unsigned int>(mesh.vertices.size());
        mesh.vertices.push_back(p);
        unique.emplace(p, id);
        return id;
    };

    for (const tinyobj::shape_t &shape : shapes) {
        const tinyobj::mesh_t &shape_mesh = shape.mesh;
        std::size_t cursor = 0; // indices 是扁平的，按 face 推进
        for (const unsigned int face_vertices : shape_mesh.num_face_vertices) {
            // triangulate=true 时 face_vertices 恒为 3；多边形用扇形兜底。
            for (unsigned int k = 1; k + 1 < face_vertices; ++k) {
                const tinyobj::index_t corners[3] = {
                    shape_mesh.indices[cursor],
                    shape_mesh.indices[cursor + k],
                    shape_mesh.indices[cursor + k + 1]};

                unsigned int tri[3];
                bool valid = true;
                for (int c = 0; c < 3; ++c) {
                    const std::optional<unsigned int> id =
                        intern(corners[c].vertex_index);
                    if (!id) {
                        valid = false;
                        break;
                    }
                    tri[c] = *id;
                }
                // 必须整个三角形一起提交，否则索引流会错位。
                if (valid) {
                    mesh.indices.insert(mesh.indices.end(),
                                        {tri[0], tri[1], tri[2]});
                }
            }
            cursor += face_vertices;
        }
    }

    if (mesh.indices.empty()) {
        if (out_error != nullptr) {
            *out_error = std::format("obj contains no triangles: {}", path);
        }
        return std::nullopt;
    }

    return mesh;
}

} // namespace qiezi
