#include "rasterizer.hpp"
#include "define.hpp"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/gtc/matrix_inverse.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace {
uint32_t color_to_uint32(const Color &color) {
    const auto r = static_cast<uint32_t>(glm::clamp(color.r, 0.f, 1.f) * 255.f);
    const auto g = static_cast<uint32_t>(glm::clamp(color.g, 0.f, 1.f) * 255.f);
    const auto b = static_cast<uint32_t>(glm::clamp(color.b, 0.f, 1.f) * 255.f);
    return (r << 16) | (g << 8) | b;
}

/// 用 Sutherland-Hodgman 算法把多边形裁到 w > eps 的半空间。
///
/// 逐边处理，四种情况:
///   in  -> in : 只输出终点
///   in  -> out: 只输出交点
///   out -> in : 输出交点 + 终点
///   out -> out: 什么都不输出
///
/// 三角形裁完是 0、1 或 2 个三角形（3 或 4 个顶点），调用方扇形三角化。
std::size_t clip_near(const Rasterizer::ClipVertex in[3],
                      Rasterizer::ClipVertex out[4]) {
    std::size_t n = 0;
    for (int i = 0; i < 3; ++i) {
        const Rasterizer::ClipVertex &cur = in[i];
        const Rasterizer::ClipVertex &next = in[(i + 1) % 3];
        const bool cur_in = cur.clip.w > consts::kNearPlaneEps;
        const bool next_in = next.clip.w > consts::kNearPlaneEps;

        if (cur_in) {
            if (n < 4) {
                out[n++] = cur;
            }
            if (!next_in) {
                // 出边：加交点
                const float t = (consts::kNearPlaneEps - cur.clip.w) /
                                (next.clip.w - cur.clip.w);
                if (n < 4) {
                    out[n++] = Rasterizer::interpolate(cur, next, t);
                }
            }
        } else if (next_in) {
            // 入边：加交点
            const float t = (consts::kNearPlaneEps - cur.clip.w) /
                            (next.clip.w - cur.clip.w);
            if (n < 4) {
                out[n++] = Rasterizer::interpolate(cur, next, t);
            }
        }
        // out -> out: 不输出
    }
    return n;
}
} // namespace

Rasterizer::Rasterizer(View view)
    : buffer_(view.w, view.h), zbuffer_(view.w, view.h),
      view_(std::move(view)) {
    const int w = view_.w;
    const int h = view_.h;
    view_matrix_ = glm::lookAtRH(view_.eye, view_.center, view_.up);
    projection_ = glm::perspectiveFovRH_ZO(view_.fov,
                                           static_cast<float>(w),
                                           static_cast<float>(h),
                                           view_.n,
                                           view_.f);
    viewport_ = {
        {w / 2.f, 0, 0, 0},
        {0, h / 2.f, 0, 0},
        {0, 0, 1.f, 0},
        {w / 2.f, h / 2.f, 0.f, 1.f},
    };
}

void Rasterizer::set_pixel(const glm::i32vec2 &point, const Color &color) {
    buffer_(point) = color_to_uint32(color);
}

void Rasterizer::set_pixel(int x, int y, const Color &color) {
    buffer_(x, y) = color_to_uint32(color);
}

void Rasterizer::clear(BufferType bt) {
    if ((bt & BufferType::kColor) == BufferType::kColor) {
        std::fill_n(buffer_.data(), buffer_.size(), 0u);
    }
    if ((bt & BufferType::kDepth) == BufferType::kDepth) {
        std::fill_n(zbuffer_.data(),
                    zbuffer_.size(),
                    std::numeric_limits<float>::max());
    }
}

Rasterizer::ClipVertex
Rasterizer::interpolate(const ClipVertex &a, const ClipVertex &b, float t) {
    ClipVertex r;
    r.clip = glm::mix(a.clip, b.clip, t);
    r.world_position = glm::mix(a.world_position, b.world_position, t);
    r.normal = glm::mix(a.normal, b.normal, t);
    r.texcoord = glm::mix(a.texcoord, b.texcoord, t);
    r.color = glm::mix(a.color, b.color, t);
    return r;
}

void Rasterizer::draw(const IndexedMesh &mesh) {
    const glm::mat4 mv = view_matrix_ * model_;

    // 每次 draw 重新构建上下文 —— 矩阵永远是最新的，
    // 不像 lambda 捕获那样会在相机/模型改变后过期
    ShaderContext ctx;
    ctx.mvp = projection_ * mv;
    ctx.model_view = mv;
    ctx.model = model_;
    ctx.normal_matrix = glm::inverseTranspose(glm::mat3{model_});
    ctx.camera_position = view_.eye;
    // 光源矩阵由调用方设置（shadow mapping 用）。这里只保证它是"能用的"。
    ctx.light_view_projection = light_view_projection_;

    // 没设 shader 时用内置默认实现。
    // 用指针而不是引用：三目运算符要求两分支同类型，引用会逼出 static_cast。
    // DefaultShader 无状态且只读，做成函数内 static 只构造一次。
    static const DefaultShader default_shader;
    const Shader *shader = shader_ ? shader_.get() : &default_shader;

    // 顶点处理：每个唯一顶点一次。这里只算裁剪空间位置和 varying，
    // 透视除法和视口变换推迟到【裁剪之后】做 —— 因为裁剪会产生新顶点。
    clip_.resize(mesh.vertices.size());

    for (std::size_t v = 0; v < mesh.vertices.size(); ++v) {
        const Vertex &src = mesh.vertices[v];

        const VertexIn in{
            glm::vec3{src.position}, src.normal, src.texcoord, src.color};
        const std::optional<VertexOut> out = shader->vertex(in, ctx);
        if (!out) {
            clip_[v].clip.w = 0.f; // 标记为丢弃
            continue;
        }
        clip_[v].clip = out->clip_position;
        clip_[v].world_position = out->world_position;
        clip_[v].normal = out->normal;
        clip_[v].texcoord = out->texcoord;
        clip_[v].color = out->color;
    }

    // 图元装配 -> 近平面裁剪 -> 透视除法 -> 视口变换 -> 光栅化
    //
    // 输出缓冲按最坏情况预留：每个三角形裁完最多 2 个三角形
    xformed_.resize(mesh.indices.size());

    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        Rasterizer::ClipVertex poly_in[3];
        bool valid = true;
        for (int c = 0; c < 3; ++c) {
            const uint32_t id = mesh.indices[i + static_cast<std::size_t>(c)];
            if (id >= clip_.size() || clip_[id].clip.w == 0.f) {
                valid = false; // 越界索引，或顶点被 shader 丢弃
                break;
            }
            poly_in[c] = clip_[id];
        }
        if (!valid) {
            continue;
        }

        // 快速路径：整个三角形都在近平面内（绝大多数情况）
        Rasterizer::ClipVertex poly[4];
        std::size_t count = 0;
        if (poly_in[0].clip.w > consts::kNearPlaneEps &&
            poly_in[1].clip.w > consts::kNearPlaneEps &&
            poly_in[2].clip.w > consts::kNearPlaneEps) {
            poly[0] = poly_in[0];
            poly[1] = poly_in[1];
            poly[2] = poly_in[2];
            count = 3;
        } else {
            count = clip_near(poly_in, poly);
            if (count < 3) {
                continue; // 整个三角形被裁掉
            }
        }

        // 扇形三角化：3 个顶点 -> 1 个三角形，4 个顶点 -> 2 个三角形
        for (std::size_t k = 1; k + 1 < count; ++k) {
            const Rasterizer::ClipVertex *tri_in[3] = {
                &poly[0], &poly[k], &poly[k + 1]};
            Triangle tri;
            for (int c = 0; c < 3; ++c) {
                const Rasterizer::ClipVertex &cv = *tri_in[c];
                const float inv_w = 1.f / cv.clip.w;
                const glm::vec3 ndc{
                    cv.clip.x * inv_w, cv.clip.y * inv_w, cv.clip.z * inv_w};
                const glm::vec4 screen = viewport_ * glm::vec4{ndc, 1.f};

                Vertex &dst = tri[c];
                dst.position = glm::vec4{screen.x, screen.y, ndc.z, inv_w};
                dst.world_position = cv.world_position;
                dst.normal = cv.normal;
                dst.texcoord = cv.texcoord;
                dst.color = cv.color;
            }
            rasterize_triangle(tri, shader, ctx);
        }
    }
}

void Rasterizer::rasterize_triangle(const Triangle &tri,
                                    const Shader *shader,
                                    const ShaderContext &ctx) {
    // ABC 必须用【屏幕空间】坐标 —— 后面拿屏幕像素去乘它
    const glm::mat3 ABC{
        {tri[0].position.x, tri[0].position.y, 1.f},
        {tri[1].position.x, tri[1].position.y, 1.f},
        {tri[2].position.x, tri[2].position.y, 1.f},
    };

    if (glm::determinant(ABC) < consts::kMinTriangleArea2) {
        return; // 背面 + 亚像素三角形
    }

    // 逐三角形的切线基数据（世界空间）—— 整个三角形内是常量，只算一次
    const glm::vec3 edge1 = tri[1].world_position - tri[0].world_position;
    const glm::vec3 edge2 = tri[2].world_position - tri[0].world_position;
    const glm::vec2 duv1 = tri[1].texcoord - tri[0].texcoord;
    const glm::vec2 duv2 = tri[2].texcoord - tri[0].texcoord;
    const glm::vec3 face_normal = glm::normalize(glm::cross(edge1, edge2));

    const auto [bbminx, bbmaxx] =
        std::minmax({tri[0].position.x, tri[1].position.x, tri[2].position.x});
    const auto [bbminy, bbmaxy] =
        std::minmax({tri[0].position.y, tri[1].position.y, tri[2].position.y});

    const int x0 = std::max(0, static_cast<int>(std::floor(bbminx)));
    const int x1 = std::min(view_.w - 1, static_cast<int>(std::ceil(bbmaxx)));
    const int y0 = std::max(0, static_cast<int>(std::floor(bbminy)));
    const int y1 = std::min(view_.h - 1, static_cast<int>(std::ceil(bbmaxy)));
    if (x0 > x1 || y0 > y1) {
        return;
    }

    const glm::mat3 inv = glm::inverse(ABC);

    // ------------------------------------------------------------------
    // SSAA：每个像素取 consts::kSubSampleCount 个子样本，各自做覆盖测试和
    // 着色，最后把颜色平均后写一次。
    //
    // 深度缓冲仍然【逐像素】只有一个值，所以：
    //   - 每个子样本各自和它比较（等价于"深度测试通过就保留"）
    //   - 写回的是【通过测试的子样本的平均深度】
    // 这样同一个像素内多个三角形争夺时，先画的浅色会被后画的挡掉，
    // 行为和单样本时一致。
    //
    // 注意这不是解析式抗锯齿（AA），是 4x 超采样 —— 边缘改善明显但
    // 不是完美的，斜边仍会有阶梯，只是台阶细了一半。
    // ------------------------------------------------------------------
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const float current_z = zbuffer_(x, y);

            Color accum_color{0.f};
            float accum_z = 0.f;
            int hits = 0;

            for (int s = 0; s < consts::kSubSampleCount; ++s) {
                const float sx =
                    static_cast<float>(x) + consts::kSubSampleOffset[s][0];
                const float sy =
                    static_cast<float>(y) + consts::kSubSampleOffset[s][1];

                // 覆盖测试：重心坐标有负分量 = 在三角形外
                const glm::vec3 bc = inv * glm::vec3{sx, sy, 1.f};
                if (bc.x < 0.f || bc.y < 0.f || bc.z < 0.f) {
                    continue;
                }

                // 深度测试（RH_ZO：z 小 = 近）
                const float ndc_z = bc.x * tri[0].position.z +
                                    bc.y * tri[1].position.z +
                                    bc.z * tri[2].position.z;
                if (ndc_z >= current_z) {
                    continue;
                }

                // 透视校正插值：屏幕空间权重乘以 1/w 再归一化
                glm::vec3 w{bc.x * tri[0].position.w,
                            bc.y * tri[1].position.w,
                            bc.z * tri[2].position.w};
                w /= (w.x + w.y + w.z);

                FragmentIn fin;
                fin.screen_position = glm::vec3{sx, sy, ndc_z};
                fin.world_position = tri[0].world_position * w.x +
                                     tri[1].world_position * w.y +
                                     tri[2].world_position * w.z;
                fin.normal =
                    tri[0].normal * w.x + tri[1].normal * w.y + tri[2].normal * w.z;
                fin.texcoord = tri[0].texcoord * w.x + tri[1].texcoord * w.y +
                               tri[2].texcoord * w.z;
                fin.color = tri[0].color * w.x + tri[1].color * w.y +
                            tri[2].color * w.z;
                fin.edge1 = edge1;
                fin.edge2 = edge2;
                fin.duv1 = duv1;
                fin.duv2 = duv2;
                fin.face_normal = face_normal;

                const std::optional<Color> out = shader->fragment(fin, ctx);
                if (!out) {
                    continue; // hook 丢弃了这个子样本（镂空等）
                }

                accum_color += *out;
                accum_z += ndc_z;
                ++hits;
            }

            if (hits == 0) {
                continue; // 这个像素没有任何子样本通过
            }

            zbuffer_(x, y) = accum_z / static_cast<float>(hits);
            set_pixel(x, y, accum_color / static_cast<float>(hits));
        }
    }
}
