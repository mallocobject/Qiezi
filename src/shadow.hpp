#pragma once

// Shadow map：从光源渲染出的深度图 + 把它当阴影贴图用的采样逻辑。
//
// 流程（两遍）：
//
//   第一遍  用光源的 View 跑 Rasterizer，只保留深度
//           rasterizer.clear(kDepth); rasterizer.draw(mesh);
//           shadow_map.capture(rasterizer);          // 把深度拷出来
//
//   第二遍  用相机的 View 渲染，shader 里：
//           const float lit = shadow_map.sample(in.world_position, N);
//
// 两次渲染用的是同一个 Rasterizer::draw，只是 View 不同 —— 光源和相机在
// 管线里没有区别。

#include "buffer.hpp"
#include "define.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

class ShadowMap {
  public:
    /// 分配 size×size 的深度图。深度初值 1.0 = 光源视锥最远处。
    void resize(int size) {
        size_ = size;
        depth_.assign(static_cast<std::size_t>(size) * size, 1.f);
        valid_ = false;
    }

    /// 把光栅化器的深度缓冲拷进来。
    ///
    /// 必须拷贝：那个 Rasterizer 下一帧 clear() 就会覆盖它，而阴影贴图
    /// 要活过整个相机 pass。
    void capture(const Buffer<float> &depth) {
        size_ = depth.w();
        depth_.resize(static_cast<std::size_t>(depth.w()) * depth.h());
        for (int y = 0; y < depth.h(); ++y) {
            for (int x = 0; x < depth.w(); ++x) {
                depth_[static_cast<std::size_t>(y) * depth.w() + x] =
                    depth(x, y);
            }
        }
        valid_ = true;
    }

    void set_view_projection(const glm::mat4 &m) {
        light_view_projection_ = m;
    }

    [[nodiscard]] const glm::mat4 &view_projection() const {
        return light_view_projection_;
    }

    [[nodiscard]] int size() const {
        return size_;
    }

    [[nodiscard]] bool valid() const {
        return valid_ && size_ > 0;
    }

    [[nodiscard]] float bias() const {
        return bias_;
    }

    void set_bias(float b) {
        bias_ = b;
    }

    void set_slope_bias(bool on) {
        slope_bias_ = on;
    }

    /// 阴影测试。
    ///
    /// world_pos : 片元的世界坐标（FragmentIn::world_position）
    /// to_light  : 世界空间里"从表面指向光源"的【单位】向量。
    ///             只有开了 slope bias 才用得到，但建议都传 ——
    ///             阴影贴图自己不知道光源在哪（它只有矩阵）。
    ///
    /// 返回光照系数：1.0 = 完全受光，0.0 = 完全在阴影里。
    ///
    /// 光源视锥外的点算作【受光】
    [[nodiscard]] float sample(const glm::vec3 &world_pos,
                               const glm::vec3 &world_nrm,
                               const glm::vec3 &to_light) const {
        if (!valid()) {
            return 1.f;
        }
        const glm::vec4 lp = light_view_projection_ * glm::vec4{world_pos, 1.f};
        if (lp.w <= 0.f) {
            return 1.f; // 在光源背后
        }
        const glm::vec3 ndc = glm::vec3{lp} / lp.w;

        // 光源视锥外
        if (ndc.x < -1.f || ndc.x > 1.f || ndc.y < -1.f || ndc.y > 1.f ||
            ndc.z < 0.f || ndc.z > 1.f) {
            return 1.f;
        }

        const int sx = static_cast<int>((ndc.x * 0.5f + 0.5f) * size_);
        const int sy = static_cast<int>((ndc.y * 0.5f + 0.5f) * size_);
        if (sx < 0 || sx >= size_ || sy < 0 || sy >= size_) {
            return 1.f;
        }
        const float stored = depth_[static_cast<std::size_t>(sy) * size_ + sx];

        // bias：防止自阴影痤疮（shadow acne）。
        // slope-scaled：法线越斜，同一纹素覆盖的世界距离越大，需要更大 bias。
        float b = bias_;
        if (slope_bias_) {
            const glm::vec3 n = glm::normalize(world_nrm);
            const float ndl = std::max(0.f, glm::dot(n, to_light));
            b = bias_ * glm::clamp(1.f / std::max(ndl, 0.2f), 1.f, 6.f);
        }
        return (ndc.z - b > stored) ? 0.f : 1.f;
    }

  private:
    int size_{0};
    std::vector<float> depth_; ///< 光源 NDC z，行优先
    glm::mat4 light_view_projection_{1.f};
    float bias_{1e-4f};
    bool slope_bias_{false};
    bool valid_{false};
};
