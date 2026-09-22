#pragma once

// 最简 OBJ 加载：唯一顶点 + 三角形索引。
//
// 用法：
//   auto mesh = qiezi::load_obj("assets/xxx.obj");
//   if (!mesh) { ... }
//   for (std::size_t t = 0; t * 3 + 2 < mesh->indices.size(); ++t) {
//       const glm::vec3 &a = mesh->vertices[mesh->indices[3 * t + 0]];  //
//       0,1,2 const glm::vec3 &b = mesh->vertices[mesh->indices[3 * t + 1]]; //
//       3,4,5 const glm::vec3 &c = mesh->vertices[mesh->indices[3 * t + 2]];
//   }

#include <glm/glm.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qiezi {

/// 带索引的三角形网格。
struct Mesh {
    /// 唯一顶点位置，每个位置只存一份。
    std::vector<glm::vec3> vertices;
    /// 三角形索引，长度恒为 3 的倍数：
    /// [0,1,2] 是第一个三角形，[3,4,5] 是第二个，依此类推。
    std::vector<unsigned int> indices;

    [[nodiscard]] std::size_t triangle_count() const {
        return indices.size() / 3;
    }

    /// 取第 i 个三角形的三个顶点位置，越界返回 nullptr。
    [[nodiscard]] const glm::vec3 *triangle(std::size_t i) const {
        if (i >= triangle_count()) {
            return nullptr;
        }
        const unsigned int *idx = &indices[3 * i];
        return (idx[0] < vertices.size() && idx[1] < vertices.size() &&
                idx[2] < vertices.size())
                   ? &vertices[idx[0]]
                   : nullptr;
    }
};

/// 读取 .obj，按位置去重后返回唯一顶点和三角形索引。
/// 四边面/多边形会自动三角化。失败时返回 nullopt，原因写进 out_error。
[[nodiscard]] std::optional<Mesh> load_obj(std::string_view path,
                                           std::string *out_error = nullptr);

} // namespace qiezi
