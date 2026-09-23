#pragma once

#include "buffer.hpp"
#include "define.hpp"
#include "glm/fwd.hpp"
#include "mesh.hpp"
#include "shader.hpp"
#include "triangle.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

enum class BufferType : uint8_t {
    kColor,
    kDepth,
};

inline BufferType operator|(BufferType a, BufferType b) {
    return static_cast<BufferType>(static_cast<uint8_t>(a) |
                                   static_cast<uint8_t>(b));
}

inline BufferType operator&(BufferType a, BufferType b) {
    return static_cast<BufferType>(static_cast<uint8_t>(a) &
                                   static_cast<uint8_t>(b));
}

struct Camera {
    glm::vec3 eye{0.f};
    glm::vec3 center{0.f};
    glm::vec3 up{0.f};
    float fov;
    float n;
    float f;
    int w;
    int h;
};

class Rasterizer {
  public:
    /// 裁剪阶段的顶点：裁剪空间位置 + 相机空间位置 + 各个 varying。
    ///
    /// 在裁剪空间做裁剪，所以透视除法必须推迟到裁剪之后 —— 顶点跑到相机
    /// 后方时 clip.w <= 0，提前除法会把坐标翻转并放大到无穷。
    ///
    /// 存相机空间位置是因为透视校正插值要用它：裁剪产生新顶点时，
    /// 这个位置也必须跟着线性插值，否则新顶点的 w 是错的。
    struct ClipVertex {
        glm::vec4 clip{0.f};
        glm::vec3 view_position{0.f};
        glm::vec3 normal{0.f};
        glm::vec2 texcoord{0.f};
        Color color{0.f};
    };

    [[nodiscard]] static ClipVertex
    interpolate(const ClipVertex &a, const ClipVertex &b, float t);

    Rasterizer(Camera camera);

    ~Rasterizer() {
    }

    /// 设置着色器。传 nullptr 恢复内置默认实现。
    ///
    /// 用 shared_ptr：同一个 shader 实例可以被多个 Rasterizer 共用
    /// （多个视口 / 多个 pass 共享材质参数），生命周期自动管理。
    ///
    /// 它同时负责顶点和片段两个阶段（派生类通常只重写 fragment）。
    void set_shader(std::shared_ptr<Shader> shader) {
        shader_ = std::move(shader);
    }

    /// 直接就地构造，省掉调用方的 make_shared；返回引用好继续设参数。
    ///   LambertShader &s = r.emplace_shader<LambertShader>();
    template <typename T, typename... Args>
    T &emplace_shader(Args &&...args) {
        auto owned = std::make_shared<T>(std::forward<Args>(args)...);
        T &ref = *owned;
        shader_ = std::move(owned);
        return ref;
    }

    /// 当前 shader，没有则 nullptr。拿到的是裸指针，只用于查询。
    [[nodiscard]] Shader *shader() {
        return shader_.get();
    }

    [[nodiscard]] const Shader *shader() const {
        return shader_.get();
    }

    /// 当前的 shared_ptr 副本 —— 用来把同一个 shader 交给别的 Rasterizer。
    [[nodiscard]] std::shared_ptr<Shader> shared_shader() const {
        return shader_;
    }

    /// 帧缓冲。用 write_ppm 之类的工具读它。
    [[nodiscard]] Buffer<uint32_t> &buffer() {
        return buffer_;
    }

    [[nodiscard]] const Buffer<uint32_t> &buffer() const {
        return buffer_;
    }

    /// 清屏。draw() 不会自动调用 —— 每帧开始前必须自己调。
    void clear(BufferType bt);

    /// 绘制索引网格。每个唯一顶点只做一次顶点处理。
    void draw(const IndexedMesh &mesh);

  private:
    void set_pixel(const glm::i32vec2 &point, const Color &color);
    void set_pixel(int x, int y, const Color &color);

    void rasterize_triangle(const Triangle &tri,
                            const Shader *shader,
                            const ShaderContext &ctx);

    glm::mat4 model_{1.f};
    glm::mat4 view_{1.f};
    glm::mat4 projection_{1.f};
    glm::mat4 viewport_{1.f};

    std::shared_ptr<Shader> shader_; ///< 为空则用内置 DefaultShader

    Buffer<uint32_t> buffer_;
    Buffer<float> zbuffer_;

    /// draw() 的临时缓冲，复用以避免每帧堆分配。
    /// clip_    : 顶点处理后的【裁剪空间】数据（裁剪要用，所以不能提前做除法）
    /// xformed_ : 裁剪 + 透视除法 + 视口变换后的屏幕空间三角形顶点
    std::vector<ClipVertex> clip_;
    std::vector<Vertex> xformed_;

    Camera camera_;
};