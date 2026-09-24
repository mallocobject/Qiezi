#pragma once

// 光源。
//
// 三种类型用同一个结构体表示，靠 kind 区分 —— 比虚基类简单，而且光源是
// 纯数据（没有行为），不需要多态：
//
//   平行光（太阳）: 只有方向, 没有位置。光线平行, 不随距离衰减。
//   点光源        : 有位置, 向四面八方发光, 按距离平方衰减。
//   聚光灯        : 点光源 + 方向 + 张角（暂未实现, 加两个字段即可）。
//
// 亮度分开三种，因为物理意义不同：
//
//   ambient   环境项。这是【假】的 —— 真实的环境光来自无数方向的反弹，
//             用常数近似。通常由所有光源共担，或者单独设一个。
//   diffuse   漫反射。N·L 那项。
//   specular  高光。Blinn-Phong 那项。
//
// 可以只给 ambient（当补光用）、只给 diffuse（哑光材质）、或三者都给。

#include "define.hpp"

#include <vector>

struct Light {
    enum class Kind {
        kDirectional, ///< 平行光：只用 direction
        kPoint,       ///< 点光源：只用 position
    };

    Kind kind{Kind::kPoint};

    glm::vec3 position{0.f};  ///< 点光源的位置（世界空间）
    glm::vec3 direction{0.f, -1.f, 0.f}; ///< 平行光的【入射方向】（世界空间）

    Color ambient{0.f};  ///< 环境项系数
    Color diffuse{1.f};  ///< 漫反射系数（也当颜色用）
    Color specular{1.f}; ///< 高光系数

    float shininess{35.f}; ///< 高光指数，逐光源可调

    /// 平行光：光线【来的方向】= -direction。
    /// 点光源：从表面指向光源（逐片段不同，这里只给个静态版本）。
    [[nodiscard]] glm::vec3 to_light(const glm::vec3 &surface) const {
        return (kind == Kind::kDirectional)
                   ? -glm::normalize(direction)
                   : glm::normalize(position - surface);
    }
};

/// 定向光（太阳/主光）。
[[nodiscard]] inline Light make_directional(const glm::vec3 &direction,
                                            const Color &diffuse) {
    Light l;
    l.kind = Light::Kind::kDirectional;
    l.direction = direction;
    l.diffuse = diffuse;
    return l;
}

/// 点光源。
[[nodiscard]] inline Light make_point(const glm::vec3 &position,
                                      const Color &diffuse) {
    Light l;
    l.kind = Light::Kind::kPoint;
    l.position = position;
    l.diffuse = diffuse;
    return l;
}

using LightList = std::vector<Light>;
