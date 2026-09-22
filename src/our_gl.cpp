#include "our_gl.h"
#include "glm/fwd.hpp"
#include <algorithm>
#include <cstdint>
#include <memory>

std::unique_ptr<float[]> zbuffer;
glm::mat4 model_view, viewport, perspective;

void init_zbuffer(int w, int h) {
    zbuffer = std::make_unique<float[]>((size_t)w * h);
    std::fill_n(
        zbuffer.get(), (size_t)w * h, -std::numeric_limits<float>::max());
}

void init_viewport(float x, float y, int w, int h) {
    viewport = {
        {w / 2.f, 0, 0, 0},
        {0, h / 2.f, 0, 0},
        {0, 0, 1, 0},
        {w / 2.f + x, h / 2.f + y, 0, 1.f},
    };
}

void init_perspective(float f) {
    perspective = {
        {1.f, 0, 0, 0},
        {0, 1.f, 0, 0},
        {0, 0, 1.f, -1.f / f},
        {0, 0, 0, 1.f},
    };
}

void lookat(const glm::vec3 &eye,
            const glm::vec3 &center,
            const glm::vec3 &up) {
    const glm::vec3 n = glm::normalize(eye - center);
    const glm::vec3 l = glm::normalize(glm::cross(up, n));
    const glm::vec3 m = glm::normalize(glm::cross(n, l));

    model_view = {glm::transpose(glm::mat4{
                      {l, 0},
                      {m, 0},
                      {n, 0},
                      {0, 0, 0, 1.f},
                  }) *
                  glm::mat4{
                      {1.f, 0, 0, 0},
                      {0, 1.f, 0, 0},
                      {0, 0, 1.f, 0},
                      {-center, 1.f},
                  }};
}

inline uint32_t color_to_uint32(const Color &color) {
    uint32_t r = glm::clamp(
        static_cast<uint32_t>(std::round(color.r * 255.f)), 0u, 255u);
    uint32_t g = glm::clamp(
        static_cast<uint32_t>(std::round(color.g * 255.f)), 0u, 255u);
    uint32_t b = glm::clamp(
        static_cast<uint32_t>(std::round(color.b * 255.f)), 0u, 255u);

    return (r << 16) | (g << 8) | b;
}

// inline void plot(const glm::i32vec2 &dot, const Color &color) {
//     buffer[dot.y * WIDTH + dot.x] = color_to_uint32(color);
// }

// inline void plotz(const glm::i32vec2 &dot, float z) {
//     zbuffer[dot.y * WIDTH + dot.x] = z;
// }

void rasterize(const Triangle &clip, const IShader &shader, Buffer &buffer) {
    const glm::vec4 ndc[3]{
        clip[0] / clip[0].w,
        clip[1] / clip[1].w,
        clip[2] / clip[2].w,
    };
    const glm::vec2 screen[3]{
        viewport * ndc[0],
        viewport * ndc[1],
        viewport * ndc[2],
    };

    const glm::mat3 ABC{
        {screen[0], 1},
        {screen[1], 1},
        {screen[2], 1},
    };

    if (glm::determinant(ABC) < 1.f) {
        return;
    }

    auto [bbminx, bbmaxx] =
        std::minmax({screen[0].x, screen[1].x, screen[2].x});
    auto [bbminy, bbmaxy] =
        std::minmax({screen[0].y, screen[1].y, screen[2].y});

    for (int y = std::max<int>(bbminy, 0);
         y <= std::min<int>(bbmaxy, buffer.h - 1);
         y++) {
        for (int x = std::max<int>(bbminx, 0);
             x <= std::min<int>(bbmaxx, buffer.w - 1);
             x++) {
            const glm::vec3 dot{x + 0.5f, y + 0.5f, 1.f}; // 像素中心
            const glm::vec3 bc = glm::inverse(ABC) * dot;
            if (bc.x >= 0 && bc.y >= 0 && bc.z >= 0) {
                float z = glm::dot(bc, glm::vec3{ndc[0].z, ndc[1].z, ndc[2].z});
                if (z > zbuffer[y * buffer.w + x]) {
                    if (auto op = shader.fragment(bc)) {
                        zbuffer[dot.y * buffer.w + dot.x] = z;
                        buffer(x, y) = color_to_uint32(*op);
                    }
                }
            }
        }
    }
}