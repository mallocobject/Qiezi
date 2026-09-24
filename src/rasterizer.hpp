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

/// 一个视图：从某个位置、朝某个方向、用某个投影看出去。
///
///   | 字段     | 相机       | 光源                     |
///   |----------|------------|--------------------------|
///   | eye      | 眼睛在哪   | 光源在哪                 |
///   | center   | 看向哪     | 照向哪                   |
///   | up       | 头顶朝哪   | 深度图的"上"方向         |
///   | fov      | 视角张角   | 聚光灯张角               |
///   | n / f    | 近/远平面  | 多近开始、多远结束记录深度 |
///   | w / h    | 出图分辨率 | 阴影贴图分辨率           |
///
/// 所以 shadow mapping 的第一遍就是"换一组 View 跑同一个 Rasterizer"。
struct View {
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
    /// 裁剪阶段的顶点：裁剪空间位置 + 世界空间位置 + 各个 varying。
    ///
    /// 在裁剪空间做裁剪，所以透视除法必须推迟到裁剪之后 —— 顶点跑到相机
    /// 后方时 clip.w <= 0，提前除法会把坐标翻转并放大到无穷。
    ///
    /// 存世界空间位置是因为它要作为 varying 传下去（光照、阴影投影、
    /// TBN 的边向量都用它）：裁剪产生新顶点时，这个位置也必须跟着线性
    /// 插值，否则新顶点会飘。
    struct ClipVertex {
        glm::vec4 clip{0.f};
        glm::vec3 world_position{0.f};
        glm::vec3 normal{0.f};
        glm::vec2 texcoord{0.f};
        Color color{0.f};
    };

    [[nodiscard]] static ClipVertex
    interpolate(const ClipVertex &a, const ClipVertex &b, float t);

    explicit Rasterizer(View view);

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

    /// 深度缓冲的访问。
    ///
    /// 给 shadow mapping 用：第一遍从光源渲染只写深度，第二遍采样它当阴影贴图。
    /// 注意这是【只读】的 —— 光栅化器自己管理它的写入。
    [[nodiscard]] const Buffer<float> &depth_buffer() const {
        return zbuffer_;
    }

    /// 设置光源的 view * projection，会填进 ShaderContext::light_view_projection。
    /// shadow mapping 的相机 pass 需要它把世界坐标投影回光源空间。
    void set_light_view_projection(const glm::mat4 &m) {
        light_view_projection_ = m;
    }

    /// 直接换一套 view/projection 矩阵，绕开 View 结构。
    ///
    /// 用途：想用非透视投影（比如太阳的正交矩阵），或者只想临时换个矩阵
    /// 而不想重建 Rasterizer。注意这会和构造时的 View 不一致 —— 换了之后
    /// ShaderContext::viewer_position 仍然是构造时那个 eye。
    void set_view_projection(const glm::mat4 &view,
                             const glm::mat4 &projection) {
        view_matrix_ = view;
        projection_ = projection;
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
    glm::mat4 view_matrix_{1.f};
    glm::mat4 projection_{1.f};
    glm::mat4 viewport_{1.f};
    glm::mat4 light_view_projection_{1.f};

    std::shared_ptr<Shader> shader_; ///< 为空则用内置 DefaultShader

    Buffer<uint32_t> buffer_;
    Buffer<float> zbuffer_;

    /// draw() 的临时缓冲，复用以避免每帧堆分配。
    /// clip_    : 顶点处理后的【裁剪空间】数据（裁剪要用，所以不能提前做除法）
    /// xformed_ : 裁剪 + 透视除法 + 视口变换后的屏幕空间三角形顶点
    std::vector<ClipVertex> clip_;
    std::vector<Vertex> xformed_;

    View view_;
};