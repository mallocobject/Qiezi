#include "texture.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <format>
#include <vector>

namespace {

/// TGA 头，18 字节。字段按 little-endian。
struct TgaHeader {
    std::uint8_t id_length;
    std::uint8_t color_map_type;
    std::uint8_t image_type; // 2=真彩, 3=灰度, 10=RLE真彩, 11=RLE灰度
    std::uint16_t color_map_first;
    std::uint16_t color_map_length;
    std::uint8_t color_map_entry_size;
    std::uint16_t x_origin;
    std::uint16_t y_origin;
    std::uint16_t width;
    std::uint16_t height;
    std::uint8_t bits_per_pixel;
    std::uint8_t descriptor; // bit5: 1=自上而下, 0=自下而上
};

std::uint16_t read_u16(const std::uint8_t *p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

bool parse_header(const std::vector<std::uint8_t> &data, TgaHeader &h) {
    if (data.size() < 18) {
        return false;
    }
    h.id_length = data[0];
    h.color_map_type = data[1];
    h.image_type = data[2];
    h.color_map_first = read_u16(&data[3]);
    h.color_map_length = read_u16(&data[5]);
    h.color_map_entry_size = data[7];
    h.x_origin = read_u16(&data[8]);
    h.y_origin = read_u16(&data[10]);
    h.width = read_u16(&data[12]);
    h.height = read_u16(&data[14]);
    h.bits_per_pixel = data[16];
    h.descriptor = data[17];
    return true;
}

} // namespace

std::optional<Texture> Texture::LoadTga(std::string_view path,
                                        std::string *out_error) {
    const auto fail = [&](std::string msg) -> std::optional<Texture> {
        if (out_error != nullptr) {
            *out_error = std::move(msg);
        }
        return std::nullopt;
    };

    FILE *fp = std::fopen(path.data(), "rb");
    if (fp == nullptr) {
        return fail(std::format("cannot open texture: {}", path));
    }
    std::fseek(fp, 0, SEEK_END);
    const long size = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(fp);
        return fail(std::format("empty texture: {}", path));
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    const std::size_t got = std::fread(data.data(), 1, data.size(), fp);
    std::fclose(fp);
    if (got != data.size()) {
        return fail(std::format("short read: {}", path));
    }

    TgaHeader h{};
    if (!parse_header(data, h)) {
        return fail(std::format("file too small to be TGA: {}", path));
    }
    if (h.color_map_type != 0) {
        return fail("TGA with color map is not supported");
    }

    const bool rle =
        (h.image_type == 10 || h.image_type == 11); // RLE 真彩 / RLE 灰度
    const bool gray = (h.image_type == 3 || h.image_type == 11);
    const bool truecolor = (h.image_type == 2 || h.image_type == 10);
    if (!gray && !truecolor) {
        return fail(std::format("unsupported TGA image type {}",
                                static_cast<int>(h.image_type)));
    }
    if (h.bits_per_pixel != 24 && h.bits_per_pixel != 32 &&
        !(gray && h.bits_per_pixel == 8)) {
        return fail(std::format("unsupported TGA bit depth {}",
                                static_cast<int>(h.bits_per_pixel)));
    }

    const int width = h.width;
    const int height = h.height;
    if (width <= 0 || height <= 0) {
        return fail("TGA has zero size");
    }

    const std::size_t bytes_per_pixel = h.bits_per_pixel / 8;
    const std::size_t pixel_count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

    // ---- 解码成 BGR(A) 字节流，顺序保持文件里的原样 ----
    std::vector<std::uint8_t> raw(pixel_count * bytes_per_pixel);
    const std::size_t offset = 18 + h.id_length;

    if (rle) {
        std::size_t src = offset;
        std::size_t dst = 0;
        while (dst < pixel_count) {
            if (src >= data.size()) {
                return fail("truncated TGA RLE data");
            }
            const std::uint8_t packet = data[src++];
            const std::size_t count =
                static_cast<std::size_t>(packet & 0x7F) + 1;
            if ((packet & 0x80) != 0) {
                // RLE 包：一个像素重复 count 次
                if (src + bytes_per_pixel > data.size()) {
                    return fail("truncated TGA RLE packet");
                }
                for (std::size_t i = 0; i < count && dst < pixel_count; ++i) {
                    for (std::size_t b = 0; b < bytes_per_pixel; ++b) {
                        raw[(dst + i) * bytes_per_pixel + b] = data[src + b];
                    }
                }
                src += bytes_per_pixel;
            } else {
                // 原始包：count 个连续像素
                const std::size_t bytes = count * bytes_per_pixel;
                if (src + bytes > data.size()) {
                    return fail("truncated TGA raw packet");
                }
                for (std::size_t b = 0;
                     b < bytes && (dst * bytes_per_pixel + b) < raw.size();
                     ++b) {
                    raw[dst * bytes_per_pixel + b] = data[src + b];
                }
                src += bytes;
            }
            dst += count;
        }
    } else {
        const std::size_t bytes = pixel_count * bytes_per_pixel;
        if (offset + bytes > data.size()) {
            return fail("truncated TGA pixel data");
        }
        for (std::size_t i = 0; i < bytes; ++i) {
            raw[i] = data[offset + i];
        }
    }

    // ---- 转成 Color，顺便把 y 翻过来 ----
    // TGA 默认是"自下而上"（descriptor bit5 = 0），此时第一行对应 v=0，
    // 正好和我们的约定一致，不用翻；只有 bit5 = 1 时才要翻。
    Texture tex;
    tex.w_ = width;
    tex.h_ = height;
    tex.pixels_.resize(pixel_count);

    const bool top_down = (h.descriptor & 0x20) != 0;
    const bool right_to_left = (h.descriptor & 0x10) != 0;
    for (int y = 0; y < height; ++y) {
        const int src_row = top_down ? (height - 1 - y) : y;
        for (int x = 0; x < width; ++x) {
            const int src_x = right_to_left ? (width - 1 - x) : x;
            const std::size_t i =
                (static_cast<std::size_t>(src_row) * width + src_x) *
                bytes_per_pixel;
            // TGA 是 BGR(A) 顺序
            const float b = static_cast<float>(raw[i + 0]) / 255.f;
            const float g = static_cast<float>(raw[i + 1]) / 255.f;
            const float r = static_cast<float>(raw[i + 2]) / 255.f;
            tex.pixels_[static_cast<std::size_t>(y) * width + x] =
                Color{r, g, b};
        }
    }

    return tex;
}

Color Texture::at(int x, int y) const {
    if (x < 0 || x >= w_ || y < 0 || y >= h_) {
        return Color{0.f};
    }
    return pixels_[static_cast<std::size_t>(y) * w_ + x];
}

Color Texture::sample_nearest(glm::vec2 uv) const {
    const float u = glm::clamp(uv.x, 0.f, 1.f);
    const float v = glm::clamp(uv.y, 0.f, 1.f);
    const int x =
        glm::clamp(static_cast<int>(u * static_cast<float>(w_)), 0, w_ - 1);
    const int y =
        glm::clamp(static_cast<int>(v * static_cast<float>(h_)), 0, h_ - 1);
    return at(x, y);
}

Color Texture::sample(glm::vec2 uv) const {
    const float u = glm::clamp(uv.x, 0.f, 1.f);
    const float v = glm::clamp(uv.y, 0.f, 1.f);

    // 像素中心的坐标（0.5 偏移），再减去半像素作为插值基点
    const float fx = u * static_cast<float>(w_) - 0.5f;
    const float fy = v * static_cast<float>(h_) - 0.5f;
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);

    const Color c00 = at(x0, y0);
    const Color c10 = at(x0 + 1, y0);
    const Color c01 = at(x0, y0 + 1);
    const Color c11 = at(x0 + 1, y0 + 1);

    const Color a = glm::mix(c00, c10, tx);
    const Color b = glm::mix(c01, c11, tx);
    return glm::mix(a, b, ty);
}
