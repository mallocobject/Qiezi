#include "glm/ext/quaternion_geometric.hpp"
#include "glm/fwd.hpp"
#include "glm/geometric.hpp"
#include "glm/matrix.hpp"
#include "obj_vertices.h"
#include "our_gl.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <iterator>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
#include <ranges>
#include <string>
#include <sys/types.h>
#include <tuple>
#include <utility>
#include <vector>

using Color = glm::vec3;
namespace rg = std::ranges;

inline constexpr Color WHITE{1.f, 1.f, 1.f};
inline constexpr Color BLACK{0, 0, 0};
inline constexpr Color RED{1.f, 0, 0};
inline constexpr Color GREEN{0, 1.f, 0};
inline constexpr Color BLUE{0, 0, 1.f};

inline constexpr const int WIDTH = 800;
inline constexpr const int HEIGHT = 800;

extern std::unique_ptr<float[]> zbuffer;
extern glm::mat4 model_view, perspective;

namespace detail {
template <typename T>
concept Arithmetic = std::is_arithmetic_v<T> && !std::same_as<T, bool>;

template <Arithmetic T>
struct Random {
    T operator()(T a = 0, T b = 1) const {
        thread_local std::mt19937 rng{std::random_device{}()};

        if constexpr (std::integral<T>) {
            assert(a <= b);
            std::uniform_int_distribution<T> dist(a, b);
            return dist(rng);
        } else {
            assert(a < b);
            thread_local std::uniform_real_distribution<T> dist{T{0}, T{1}};
            return a + (b - a) * dist(rng);
        }
    }
};

} // namespace detail

inline constexpr detail::Random<float> random_float{};
inline constexpr detail::Random<int> random_int{};

// 把帧缓冲写成 PPM(P6)。头部是文本，后面直接跟 RGB 字节，
bool write_ppm(const char *path, Buffer &buffer) {
    FILE *fp = std::fopen(path, "wb");
    if (fp == nullptr) {
        std::fprintf(stderr, "cannot open %s for writing\n", path);
        return false;
    }

    std::fprintf(fp, "P6\n%d %d\n255\n", WIDTH, HEIGHT);

    // 打包成连续 RGB 再一次性写出，比逐像素 fputc 快得多。
    std::vector<unsigned char> rgb(static_cast<size_t>(WIDTH) * HEIGHT * 3);
    for (int y = 0; y < HEIGHT; ++y) {
        const int src_y = HEIGHT - 1 - y; // 源行号倒过来
        for (int x = 0; x < WIDTH; ++x) {
            const uint32_t px = buffer(x, src_y);
            const size_t o = (static_cast<size_t>(y) * WIDTH + x) * 3;
            rgb[o + 0] = static_cast<unsigned char>((px >> 16) & 0xFF);
            rgb[o + 1] = static_cast<unsigned char>((px >> 8) & 0xFF);
            rgb[o + 2] = static_cast<unsigned char>(px & 0xFF);
        }
    }

    const size_t written = std::fwrite(rgb.data(), 1, rgb.size(), fp);
    std::fclose(fp);
    if (written != rgb.size()) {
        std::fprintf(stderr, "short write to %s\n", path);
        return false;
    }
    return true;
}

// bool write_pgm(const char *path) {
//     FILE *fp = std::fopen(path, "wb");
//     if (fp == nullptr) {
//         std::fprintf(stderr, "cannot open %s for writing\n", path);
//         return false;
//     }

//     std::fprintf(fp, "P5\n%d %d\n255\n", WIDTH, HEIGHT);

//     // 灰度每像素 1 字节，不再是 3 字节
//     std::vector<unsigned char> gray(static_cast<size_t>(WIDTH) * HEIGHT);
//     for (int i = gray.size() - 1; i >= 0; --i) {
//         // 取一个通道即可，灰度时 R=G=B
//         gray[i] = static_cast<unsigned
//         char>(static_cast<uint32_t>(zbuffer[i]) &
//                                              0xFF);
//     }

//     const size_t written = std::fwrite(gray.data(), 1, gray.size(), fp);
//     std::fclose(fp);
//     return written == gray.size();
// }

// inline uint32_t color_to_uint32(const Color &color) {
//     uint32_t r = glm::clamp(
//         static_cast<uint32_t>(std::round(color.r * 255.f)), 0u, 255u);
//     uint32_t g = glm::clamp(
//         static_cast<uint32_t>(std::round(color.g * 255.f)), 0u, 255u);
//     uint32_t b = glm::clamp(
//         static_cast<uint32_t>(std::round(color.b * 255.f)), 0u, 255u);

//     return (r << 16) | (g << 8) | b;
// }

// inline void plot(const glm::i32vec2 &dot, const Color &color) {
//     buffer[dot.y * WIDTH + dot.x] = color_to_uint32(color);
// }

// inline void plotz(const glm::i32vec2 &dot, float z) {
//     zbuffer[dot.y * WIDTH + dot.x] = z;
// }

// void draw_lineH(glm::i32vec2 a, glm::i32vec2 b, const Color &color) {
//     if (a.x > b.x) {
//         std::swap(a, b);
//     }
//     int dx = b.x - a.x;
//     int dy = b.y - a.y;
//     int sign = dy > 0 ? 1 : -1;
//     dy *= sign;
//     int D = 2 * dy - dx;
//     int y = a.y;
//     for (int x = a.x; x <= b.x; x++) {
//         plot(glm::i32vec2{x, y}, color);
//         if (D > 0) {
//             y += sign;
//             D -= 2 * dx;
//         }
//         D += 2 * dy;
//     }
// }

// void draw_lineV(glm::i32vec2 a, glm::i32vec2 b, const Color &color) {
//     if (a.y > b.y) {
//         std::swap(a, b);
//     }
//     int dx = b.x - a.x;
//     int dy = b.y - a.y;
//     int sign = dx > 0 ? 1 : -1;
//     dx *= sign;
//     int D = 2 * dx - dy;
//     int x = a.x;
//     for (int y = a.y; y <= b.y; y++) {
//         plot(glm::i32vec2{x, y}, color);
//         if (D > 0) {
//             x += sign;
//             D -= 2 * dy;
//         }
//         D += 2 * dx;
//     }
// }

// inline glm::i32vec2 round_to_pixel(const glm::vec2 &p) {
//     return glm::i32vec2{static_cast<int>(std::lround(p.x)),
//                         static_cast<int>(std::lround(p.y))};
// }

// void draw_line(const glm::vec2 &a, const glm::vec2 &b, const Color &color) {
//     if (std::abs(a.x - b.x) >= std::abs(a.y - b.y)) {
//         draw_lineH(round_to_pixel(a), round_to_pixel(b), color);
//     } else {
//         draw_lineV(round_to_pixel(a), round_to_pixel(b), color);
//     }
// }

// auto bounding_box(const glm::vec2 v[3]) {
//     const glm::vec2 lo{std::min({v[0].x, v[1].x, v[2].x}),
//                        std::min({v[0].y, v[1].y, v[2].y})};
//     const glm::vec2 hi{std::max({v[0].x, v[1].x, v[2].x}),
//                        std::max({v[0].y, v[1].y, v[2].y})};
//     return std::pair<glm::i32vec2, glm::i32vec2>{
//         glm::i32vec2{std::max(static_cast<int>(std::floor(lo.x)), 0),
//                      std::max(static_cast<int>(std::floor(lo.y)), 0)},
//         glm::i32vec2{std::min(static_cast<int>(std::ceil(hi.x)), WIDTH - 1),
//                      std::min(static_cast<int>(std::ceil(hi.y)), HEIGHT -
//                      1)}};
// }

// bool is_inner_triangle(const glm::vec2 &p,
//                        const glm::vec2 &v0,
//                        const glm::vec2 &v1,
//                        const glm::vec2 &v2) {
//     const glm::vec2 v0p = p - v0;
//     const glm::vec2 v1p = p - v1;
//     const glm::vec2 v2p = p - v2;
//     const glm::vec2 v0v1 = v1 - v0;
//     const glm::vec2 v1v2 = v2 - v1;
//     const glm::vec2 v2v0 = v0 - v2;

//     const float a = v0p.x * v0v1.y - v0p.y * v0v1.x;
//     const float b = v1p.x * v1v2.y - v1p.y * v1v2.x;
//     const float c = v2p.x * v2v0.y - v2p.y * v2v0.x;

//     return a >= 0 && b >= 0 && c >= 0 || a < 0 && b < 0 && c < 0;
// }

// float dummy_area(const glm::vec2 v[3]) {
//     const glm::vec2 v0v1 = v[1] - v[0];
//     const glm::vec2 v0v2 = v[2] - v[0];

//     return v0v1.x * v0v2.y - v0v1.y * v0v2.x;
// }

// auto alpha_beta_gamma(const glm::vec2 &p,
//                       const glm::vec2 &v0,
//                       const glm::vec2 &v1,
//                       const glm::vec2 &v2) {
//     float total_area = dummy_area(v0, v1, v2);
//     if (total_area == 0) {
//         return std::tuple<float, float, float>{};
//     }
//     float inv_total_area = 1.f / total_area;
//     float alpha_area = dummy_area(p, v1, v2);
//     float beta_area = dummy_area(v0, p, v2);
//     float gamma_area = dummy_area(v0, v1, p);

//     return std::make_tuple<float, float, float>(alpha_area * inv_total_area,
//                                                 beta_area * inv_total_area,
//                                                 gamma_area * inv_total_area);
// }

// void draw_triangle(const glm::vec4 ndc[3], const Color &color) {
//     const glm::vec2 screen[3]{
//         viewport(ndc[0], WIDTH / 16.f, WIDTH / 16.f),
//         viewport(ndc[1], WIDTH / 16.f, WIDTH / 16.f),
//         viewport(ndc[2], WIDTH / 16.f, WIDTH / 16.f),
//     };

//     const glm::mat3 ABC{
//         {screen[0], 1},
//         {screen[1], 1},
//         {screen[2], 1},
//     };

//     auto [bbmin, bbmax] = bounding_box(screen);
//     if (glm::determinant(ABC) < 1.f) {
//         return;
//     }

//     for (int y = bbmin.y; y <= bbmax.y; y++) {
//         for (int x = bbmin.x; x <= bbmax.x; x++) {
//             const glm::vec3 dot{x + 0.5f, y + 0.5f, 1.f}; // 像素中心
//             const glm::vec3 bc = glm::inverse(ABC) * dot;
//             if (bc.x >= 0 && bc.y >= 0 && bc.z >= 0) {
//                 float z = glm::dot(bc, glm::vec3{ndc[0].z, ndc[1].z,
//                 ndc[2].z}); if (z > zbuffer[y * WIDTH + x]) {
//                     plotz(dot, z);
//                     plot(dot, color);
//                 }
//             }
//         }
//     }
// }

struct RandomShader : IShader {
    std::optional<Color> fragment(const glm::vec3 &bar) const {
        Color gl_color{1.f};
        glm::vec3 n =
            glm::normalize(glm::cross(tri[1] - tri[0], tri[2] - tri[0]));
        float ambinet = 0.1f;
        float diff = std::max(0.f, glm::dot(n, l));
        float spec =
            std::pow(std::max(0.f, glm::dot(glm::normalize(l + v), n)), 35);

        return gl_color * std::min(1.f, ambinet + 0.1f * diff + 0.9f * spec);
    }

    Color color;
    glm::vec3 tri[3];
    glm::vec3 l;
    glm::vec3 v;
};

// b: x
// n: y
// t: -z
int main(int argc, char **argv) {
    const char *ppm_path = (argc > 1) ? argv[1] : "out.ppm";
    const char *pgm_path = (argc > 2) ? argv[2] : "out.pgm";

    std::string error;
    std::optional<qiezi::Mesh> mesh =
        qiezi::load_obj(ASSETS "/diablo3_pose/diablo3_pose.obj", &error);
    if (!mesh) {
        std::fprintf(stderr, "load failed: %s\n", error.c_str());
        return 1;
    }

    // const auto model_to_screen = [&](const glm::vec3 &v) {
    //     return glm::vec3{(v.x + 1.f) * WIDTH * 0.5f,
    //                      (1.f - v.y) * HEIGHT * 0.5f,
    //                      (v.z + 1.f) * 255 * 0.5f};
    // };

    std::vector<unsigned int> order(mesh->triangle_count());
    std::iota(std::begin(order), std::end(order), 0u);

    // rg::sort(order, [&mesh](unsigned int lhs, unsigned int rhs) {
    //     const auto z_of = [&mesh](unsigned int tri) {
    //         const glm::vec3 &a = mesh->vertices[mesh->indices[3 * tri +
    //         0]]; const glm::vec3 &b = mesh->vertices[mesh->indices[3 *
    //         tri + 1]]; const glm::vec3 &c =
    //         mesh->vertices[mesh->indices[3 * tri + 2]]; return (a.z + b.z
    //         + c.z) / 3.f;
    //     };
    //     return z_of(lhs) < z_of(rhs);
    // });

    Buffer buffer(WIDTH, HEIGHT);

    constexpr const glm::vec3 eye{-1.f, 0, 2.f};
    constexpr const glm::vec3 center{0, 0, 0};
    constexpr const glm::vec3 up{0, 1.f, 0};

    lookat(eye, center, up);
    init_perspective(glm::length(eye - center));
    init_viewport(WIDTH / 16, HEIGHT / 16, WIDTH * 7 / 8, HEIGHT * 7 / 8);
    init_zbuffer(WIDTH, HEIGHT);

    for (unsigned int tri : order) {
        RandomShader shader;
        shader.tri[0] = mesh->vertices[mesh->indices[3 * tri + 0]];
        shader.tri[1] = mesh->vertices[mesh->indices[3 * tri + 1]];
        shader.tri[2] = mesh->vertices[mesh->indices[3 * tri + 2]];
        const glm::vec3 face_center =
            (shader.tri[0] + shader.tri[1] + shader.tri[2]) / 3.f;
        shader.l = glm::normalize(center - face_center);
        shader.v = glm::normalize(eye - face_center);
        Triangle clip{
            perspective * model_view * glm::vec4{shader.tri[0], 1.f},
            perspective * model_view * glm::vec4{shader.tri[1], 1.f},
            perspective * model_view * glm::vec4{shader.tri[2], 1.f},
        };
        for (int c = 0; c < 3; c++) {
            shader.color[c] = random_float();
        }
        rasterize(clip, shader, buffer);
    }

    if (!write_ppm(ppm_path, buffer)) {
        return 1;
    }

    return 0;
}
