#pragma once

// 索引网格：唯一顶点池 + 三角形索引。
//
// 和加载器解耦 —— 光栅化器只依赖这个类型，不依赖 OBJ。

#include "triangle.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

struct IndexedMesh {
    std::vector<Vertex> vertices;  ///< 唯一顶点，每个只存一份
    std::vector<uint32_t> indices; ///< 长度恒为 3 的倍数，[0,1,2]=第 1 个三角形

    [[nodiscard]] std::size_t triangle_count() const {
        return indices.size() / 3;
    }

    [[nodiscard]] bool empty() const {
        return indices.empty();
    }

    /// 第 i 个三角形的三个顶点（i 是三角形序号，不是顶点下标），
    /// 越界返回 nullptr。
    [[nodiscard]] const Vertex *triangle(std::size_t i) const {
        if (i >= triangle_count()) {
            return nullptr;
        }
        const uint32_t *idx = &indices[3 * i];
        if (idx[0] >= vertices.size() || idx[1] >= vertices.size() ||
            idx[2] >= vertices.size()) {
            return nullptr;
        }
        return &vertices[idx[0]];
    }

    /// 顶点色统一赋值 —— 加载器不管颜色，由调用方决定。
    void set_color(const Color &color) {
        for (Vertex &v : vertices) {
            v.color = color;
        }
    }
};
