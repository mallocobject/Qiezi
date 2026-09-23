#include "obj_loader.hpp"

#include "tinyobjloader/tiny_obj_loader.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

/// 取 .obj 所在目录，作为 .mtl 的搜索路径。
/// 不传的话 tinyobjloader 会去进程的工作目录找材质，换个启动目录就找不到。
std::string parent_dir(std::string_view path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string_view::npos
               ? std::string{}
               : std::string{path.substr(0, slash)};
}

/// OBJ 的三个独立属性池 + 面角表。中间结构，不对外暴露 ——
/// 存在的意义只是把 tinyobj 的类型挡在 .cpp 里。
struct RawObj {
    std::vector<glm::vec3> positions; // 下标 0 起始
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texcoords;

    /// 一个面角：三个属性各自的下标，-1 表示缺失
    struct Corner {
        int position = -1;
        int normal = -1;
        int texcoord = -1;
    };
    std::vector<Corner> corners; // 每 3 个一组 = 一个三角形
};

/// 从 tinyobj 的扁平索引流转成 RawObj（含 1 起始 -> 0 起始的转换）。
RawObj flatten(const tinyobj::attrib_t &attrib,
               const std::vector<tinyobj::shape_t> &shapes) {
    RawObj raw;

    const std::size_t vertex_count = attrib.vertices.size() / 3;
    const std::size_t normal_count = attrib.normals.size() / 3;
    const std::size_t texcoord_count = attrib.texcoords.size() / 2;

    raw.positions.reserve(vertex_count);
    for (std::size_t i = 0; i < vertex_count; ++i) {
        raw.positions.emplace_back(attrib.vertices[3 * i + 0],
                                   attrib.vertices[3 * i + 1],
                                   attrib.vertices[3 * i + 2]);
    }
    raw.normals.reserve(normal_count);
    for (std::size_t i = 0; i < normal_count; ++i) {
        raw.normals.emplace_back(attrib.normals[3 * i + 0],
                                 attrib.normals[3 * i + 1],
                                 attrib.normals[3 * i + 2]);
    }
    raw.texcoords.reserve(texcoord_count);
    for (std::size_t i = 0; i < texcoord_count; ++i) {
        // 注意 texcoords 是 2 个一组，不是 3 个
        raw.texcoords.emplace_back(attrib.texcoords[2 * i + 0],
                                   attrib.texcoords[2 * i + 1]);
    }

    // OBJ 用的是 1 起始下标，且允许负数（-1 = 最后定义的那个顶点）。
    // tinyobj 已经统一成 0 起始，这里只做范围校验。
    const auto resolve = [](int index, std::size_t count) -> int {
        return (index >= 0 && static_cast<std::size_t>(index) < count) ? index
                                                                      : -1;
    };

    for (const tinyobj::shape_t &shape : shapes) {
        const tinyobj::mesh_t &mesh = shape.mesh;
        std::size_t cursor = 0; // indices 是扁平的，按面推进
        for (const unsigned int face_vertices : mesh.num_face_vertices) {
            // triangulate=true 时恒为 3；多边形用扇形兜底
            for (unsigned int k = 1; k + 1 < face_vertices; ++k) {
                const tinyobj::index_t trio[3] = {mesh.indices[cursor],
                                                  mesh.indices[cursor + k],
                                                  mesh.indices[cursor + k + 1]};
                RawObj::Corner tri[3];
                bool valid = true;
                for (int c = 0; c < 3; ++c) {
                    tri[c].position = resolve(trio[c].vertex_index, vertex_count);
                    tri[c].normal = resolve(trio[c].normal_index, normal_count);
                    tri[c].texcoord =
                        resolve(trio[c].texcoord_index, texcoord_count);
                    if (tri[c].position < 0) {
                        valid = false; // 位置缺失，整个三角形作废
                    }
                }
                if (valid) {
                    raw.corners.insert(raw.corners.end(),
                                       {tri[0], tri[1], tri[2]});
                }
            }
            cursor += face_vertices;
        }
    }
    return raw;
}

/// 把一个面角展开成完整顶点。缺失的属性保持 Vertex 的默认值。
Vertex make_vertex(const RawObj &raw, const RawObj::Corner &corner) {
    Vertex v;
    v.position =
        glm::vec4{raw.positions[static_cast<std::size_t>(corner.position)], 1.f};
    if (corner.normal >= 0) {
        v.normal = raw.normals[static_cast<std::size_t>(corner.normal)];
    }
    if (corner.texcoord >= 0) {
        v.texcoord = raw.texcoords[static_cast<std::size_t>(corner.texcoord)];
    }
    // 顶点色留空：OBJ 里没有颜色信息
    return v;
}

/// 去重用的哈希：按 (position, normal, texcoord) 的位模式算。
/// 三个属性都必须参与 —— 漏掉任何一个会导致不同顶点撞进同一个桶。
/// -0.0f 归一到 +0.0f，否则解析出来的 -0 会造成伪重复顶点。
struct VertexHash {
    std::size_t operator()(const Vertex &v) const noexcept {
        const auto h = [](float f) {
            const std::uint32_t bits = std::bit_cast<std::uint32_t>(f);
            return static_cast<std::size_t>(
                std::hash<std::uint32_t>{}(bits == 0x80000000u ? 0u : bits));
        };
        const auto h3 = [&h](const glm::vec3 &vec, int shift) {
            return (h(vec.x) << shift) ^ (h(vec.y) << (shift + 11)) ^
                   (h(vec.z) << (shift + 22));
        };
        return h3(glm::vec3{v.position}, 0) ^ h3(v.normal, 3) ^
               (h(v.texcoord.x) << 7) ^ (h(v.texcoord.y) << 19);
    }
};

/// 相等比较：只比参与去重的三个属性（顶点色不参与）。
struct VertexEqual {
    bool operator()(const Vertex &a, const Vertex &b) const noexcept {
        return a.position == b.position && a.normal == b.normal &&
               a.texcoord == b.texcoord;
    }
};

/// 读文件并展平。失败时返回 nullopt 并写 out_error。
std::optional<RawObj> load_raw(std::string_view path, std::string *out_error) {
    // tinyobjloader 要 const char *，string_view 不保证以 '\0' 结尾
    const std::string filename{path};
    const std::string base_dir = parent_dir(path);

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;

    const bool ok =
        tinyobj::LoadObj(&attrib,
                         &shapes,
                         &materials,
                         &warn,
                         &err,
                         filename.c_str(),
                         base_dir.empty() ? nullptr : base_dir.c_str(),
                         /*triangulate=*/true);
    if (!ok) {
        if (out_error != nullptr) {
            *out_error =
                err.empty() ? std::format("failed to load obj: {}", path) : err;
        }
        return std::nullopt;
    }

    RawObj raw = flatten(attrib, shapes);
    if (raw.corners.empty()) {
        if (out_error != nullptr) {
            *out_error = std::format("obj contains no triangles: {}", path);
        }
        return std::nullopt;
    }
    return raw;
}

} // namespace

std::optional<IndexedMesh> ObjLoader::Load(std::string_view path,
                                           std::string *out_error) {
    const std::optional<RawObj> raw = load_raw(path, out_error);
    if (!raw) {
        return std::nullopt;
    }

    IndexedMesh mesh;
    mesh.vertices.reserve(raw->positions.size());
    mesh.indices.reserve(raw->corners.size());

    // (position, normal, texcoord) -> 唯一顶点下标
    std::unordered_map<Vertex, std::uint32_t, VertexHash, VertexEqual> unique;
    unique.reserve(raw->positions.size());

    for (const RawObj::Corner &corner : raw->corners) {
        Vertex v = make_vertex(*raw, corner);
        const auto it = unique.find(v);
        if (it != unique.end()) {
            mesh.indices.push_back(it->second);
            continue;
        }
        const auto id = static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(v);
        unique.emplace(std::move(v), id);
        mesh.indices.push_back(id);
    }

    return mesh;
}
