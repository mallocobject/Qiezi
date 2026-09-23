#pragma once

// 可编程着色阶段。
//
// 数据流：
//
//   Mesh 顶点 ──①vertex()──> 裁剪空间位置 + varying
//                                 │
//                           光栅化（固定功能：透视除法、视口映射、
//                                   覆盖测试、透视校正插值、深度测试）
//                                 │
//                                 ▼
//                           插值后的 varying ──②fragment()──> 颜色 / 丢弃
//
// 用继承而不是 std::function：虚函数调用比 std::function 快约 3 倍
// （每次都是同一个派生类，分支预测命中率极高），更重要的是 ShaderContext
// 每次调用都传，矩阵永远是最新的 —— 不用像 lambda 那样捕获后手动重设。
//
// 只需要重写关心的方法：Shader 基类已经提供了默认的 vertex()。

#include "define.hpp"

#include <optional>

/// 每次着色调用都传的全局状态。矩阵在这里，所以永远和当前相机一致。
struct ShaderContext {
    glm::mat4 mvp{1.f};             ///< model * view * projection
    glm::mat4 model_view{1.f};      ///< view * model（相机空间）
    glm::mat4 model{1.f};           ///< 模型矩阵（世界空间变换用）
    glm::vec3 camera_position{0.f}; ///< 相机在世界空间的位置
};

/// 顶点处理的输入：模型空间的一个顶点。
struct VertexIn {
    glm::vec3 position{0.f}; ///< 模型空间位置（OBJ 的 v）
    glm::vec3 normal{0.f};   ///< 模型空间法线（OBJ 的 vn）
    glm::vec2 texcoord{0.f}; ///< 纹理坐标（OBJ 的 vt）
    Color color{0.f};        ///< 顶点色（OBJ 没有，由调用方给）
};

/// 顶点处理的输出：裁剪空间位置 + 要传给片段的 varying。
struct VertexOut {
    glm::vec4 clip_position{0.f}; ///< 裁剪空间位置（光栅化器做透视除法）
    glm::vec3 view_position{0.f}; ///< 相机空间位置（varying）
    glm::vec3 normal{0.f};        ///< 相机空间法线（varying）
    glm::vec2 texcoord{0.f};      ///< varying
    Color color{0.f};             ///< varying
};

/// 片段处理的输入：光栅化器插值好的属性。
struct FragmentIn {
    glm::vec3 screen_position{0.f}; ///< 屏幕空间位置，z = ndc.z
    glm::vec3 view_position{0.f}; ///< 相机空间位置（相机在原点，看向 -z）

    glm::vec3 normal{0.f}; ///< 透视校正插值后的法线（相机空间）
    glm::vec2 texcoord{0.f}; ///< 透视校正插值后的 UV
    Color color{0.f};        ///< 透视校正插值后的顶点色

    // ---- 逐三角形的切线基数据（同一三角形内是常量）----
    //
    // 在片段里算 TBN（切线空间法线贴图要用）:
    //
    //     det = duv1.x*duv2.y - duv1.y*duv2.x          // UV 面积的 2 倍
    //     T   = (edge1*duv2.y - edge2*duv1.y) / det    // 切线（未归一化）
    //     B   = cross(N, T) * sign(det)                // 副切线，手性看 det
    //
    // 逐三角形算而不是逐顶点预计算：这个模型有 53.66% 的三角形 UV 手性翻转
    // （det<0），且顶点法线与面法线夹角平均 31°（大量硬边），
    // 逐顶点平均出的切线在硬边处会错位。

    glm::vec3 edge1{0.f}; ///< P1 - P0（相机空间）
    glm::vec3 edge2{0.f}; ///< P2 - P0（相机空间）
    glm::vec2 duv1{0.f};  ///< U1 - U0
    glm::vec2 duv2{0.f};  ///< U2 - U0
    glm::vec3 face_normal{0.f}; ///< 三角形面法线（相机空间，已归一化）
};

/// 着色器基类。
///
/// 派生类只需要重写 fragment()；vertex() 的默认实现是
/// "mvp 变换 + 法线变到相机空间 + 属性原样透传"。
struct Shader {
    virtual ~Shader() = default;

    /// 顶点处理。返回 nullopt 表示丢弃该顶点（所在三角形会被跳过）。
    virtual std::optional<VertexOut> vertex(const VertexIn &in,
                                            const ShaderContext &ctx) const {
        VertexOut out;
        out.clip_position = ctx.mvp * glm::vec4{in.position, 1.f};
        out.view_position =
            glm::vec3{ctx.model_view * glm::vec4{in.position, 1.f}};
        out.normal = glm::mat3{ctx.model_view} * in.normal;
        out.texcoord = in.texcoord;
        out.color = in.color;
        return out;
    }

    /// 片段处理。返回 nullopt 表示丢弃该片段（镂空等）。
    virtual std::optional<Color> fragment(const FragmentIn &in,
                                          const ShaderContext &ctx) const = 0;
};

/// 内置默认着色器：顶点不变换额外的东西，片段直接返回插值后的顶点色。
/// 不设置 shader 时用它，行为和以前一致。
struct DefaultShader final : public Shader {
    std::optional<Color>
    fragment(const FragmentIn &in,
             const ShaderContext & /*ctx*/) const override {
        return in.color;
    }
};
