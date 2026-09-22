#pragma once

#include "glm/fwd.hpp"
#include <cstddef>
#include <glm/glm.hpp>
#include <memory>
#include <optional>

// glm::vec3 rotate(const glm::vec3 &v) {
//     constexpr float beta = glm::pi<float>() / 6;
//     glm::mat3 Ry{{std::cos(beta), 0, -std::sin(beta)},
//                  {0, 1.f, 0},
//                  {std::sin(beta), 0, std::cos(beta)}};

//     return Ry * v;
// }

// glm::vec3 persp(const glm::vec3 &v) {
//     constexpr float c = 3.f;
//     const glm::vec3 r = v / (1 - v.z / c);

//     return {r.x, r.y, v.z};
// }

void init_zbuffer(int w, int h);

void init_viewport(float x, float y, int w, int h);

void init_perspective(float f);

void lookat(const glm::vec3 &eye, const glm::vec3 &center, const glm::vec3 &up);

using Color = glm::vec3;

struct IShader {
    virtual ~IShader() {
    }

    virtual std::optional<Color> fragment(const glm::vec3 &bar) const = 0;
};

class Buffer {
  public:
    Buffer(int w, int h) : w{w}, h{h} {
        buffer = std::make_unique<uint32_t[]>((size_t)w * h);
    }

    uint32_t &operator()(int x, int y) {
        return buffer[y * w + x];
    }

    int w;
    int h;

  private:
    std::unique_ptr<uint32_t[]> buffer;
};

// a triangle primitive is made of three ordered points
typedef glm::vec4 Triangle[3];
void rasterize(const Triangle &clip, const IShader &shader, Buffer &buffer);