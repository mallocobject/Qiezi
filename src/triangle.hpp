#pragma once

#include "define.hpp"
#include <cassert>

struct Vertex {
    /// 光栅化器内部的打包字段，各分量含义不同：
    ///   x, y  = 屏幕空间像素坐标
    ///   z     = ndc.z（深度测试用，越小越近）
    ///   w     = 1/w_clip（透视校正插值用）
    glm::vec4 position{glm::vec3{0.f}, 1.f};

    glm::vec3 view_position{0.f}; ///< 相机空间位置（varying，光照用）
    glm::vec3 normal{0.f};        ///< 相机空间法线（varying）
    glm::vec2 texcoord{0.f};      ///< varying
    Color color{0.f};             ///< varying
};

class Triangle {
  public:
    Vertex &operator[](int idx) {
        assert(idx >= 0 && idx <= 2);
        return vertices[idx];
    }

    Vertex &at(int idx) {
        return operator[](idx);
    }

    const Vertex &operator[](int idx) const {
        assert(idx >= 0 && idx <= 2);
        return vertices[idx];
    }

    const Vertex &at(int idx) const {
        return operator[](idx);
    }

  private:
    Vertex vertices[3]{};
};
