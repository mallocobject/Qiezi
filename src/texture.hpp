#pragma once

// 纹理：一张 RGB 图 + 双线性采样。
//
// 只支持 TGA（Targa）—— 手写解析最简单，且是 tinyrenderer 资源的格式：
//   - 18 字节头 + 可选 id 字段 + 像素数据
//   - 24/32 位真彩，非压缩（type 2）或 RLE 压缩（type 10）
//   - 可选灰度（type 3 / 11）
//
// 像素存储约定：**原点在左下**（第一行是 v=0），和 OpenGL 一致，
// 也和 OBJ 的 vt 一致。加载时会把 TGA 的"自上而下"存储翻过来。

#include "define.hpp"
#include "glm/fwd.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Texture {
  public:
    /// 从 TGA 文件加载。失败返回 nullopt，原因写入 out_error。
    [[nodiscard]] static std::optional<Texture>
    LoadTga(std::string_view path, std::string *out_error = nullptr);

    [[nodiscard]] int width() const noexcept {
        return w_;
    }

    [[nodiscard]] int height() const noexcept {
        return h_;
    }

    [[nodiscard]] bool empty() const noexcept {
        return pixels_.empty();
    }

    /// 最近邻采样，uv 会被 clamp 到 [0,1]。
    [[nodiscard]] Color sample_nearest(glm::vec2 uv) const;

    /// 双线性采样，uv 会被 clamp 到 [0,1]。比最近邻平滑得多。
    [[nodiscard]] Color sample(glm::vec2 uv) const;

    [[nodiscard]] Color sample_nearest(float u, float v) const {
        return sample_nearest(glm::vec2{u, v});
    }

    [[nodiscard]] Color sample(float u, float v) const {
        return sample(glm::vec2{u, v});
    }

    /// 直接拿像素（越界返回黑）。
    [[nodiscard]] Color at(int x, int y) const;

  private:
    int w_{0};
    int h_{0};
    std::vector<Color> pixels_; ///< 行优先，第 0 行对应 v=0（图像底部）
};
