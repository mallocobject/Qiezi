#include "buffer.hpp"
#include "define.hpp"
#include "glm/fwd.hpp"
#include "glm/geometric.hpp"
#include "glm/matrix.hpp"
#include "obj_loader.hpp"
#include "rasterizer.hpp"
#include "shader.hpp"
#include "texture.hpp"
#include "triangle.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

/// 把帧缓冲写成 PPM(P6)。
///
/// 帧缓冲按「屏幕坐标 y 向上」存储（viewport 不做 y 翻转，和 NDC 一致），
/// 而 PPM 的行 0 在图像顶部，所以这里做全项目【唯一】的垂直翻转。
bool write_ppm(const char *path, Buffer<uint32_t> &buffer) {
    FILE *fp = std::fopen(path, "wb");
    if (fp == nullptr) {
        std::fprintf(stderr, "cannot open %s for writing\n", path);
        return false;
    }

    const int w = buffer.w();
    const int h = buffer.h();
    std::fprintf(fp, "P6\n%d %d\n255\n", w, h);

    std::vector<unsigned char> rgb(static_cast<size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) {
        const uint32_t *src =
            buffer.data() + static_cast<size_t>(h - 1 - y) * w;
        unsigned char *dst = rgb.data() + static_cast<size_t>(y) * w * 3;
        for (int x = 0; x < w; ++x) { // 这里还是逐像素：要做 0xRRGGBB -> 3 字节
            const uint32_t px = src[x];
            dst[3 * x + 0] = static_cast<unsigned char>((px >> 16) & 0xFF);
            dst[3 * x + 1] = static_cast<unsigned char>((px >> 8) & 0xFF);
            dst[3 * x + 2] = static_cast<unsigned char>(px & 0xFF);
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

struct LambertTextureShader final : public Shader {
    std::optional<Color> fragment(const FragmentIn &in,
                                  const ShaderContext &ctx) const override {
        if (!diffuse && !normal_map) {
            return in.color;
        }

        // ---------------- 1) 由逐三角形数据构造 TBN ----------------
        //
        //   det    = duv1.x*duv2.y - duv1.y*duv2.x   （UV 面积的 2 倍）
        //   T_raw  = (edge1*duv2.y - edge2*duv1.y) / det
        //   handed = sign(det)   —— UV 镜像时副切线要反号
        //
        // 用【插值法线】正交化 —— 标准做法。它保留了"这模型是曲面"的信息，
        // 基础明暗是平滑的。理论误差（插值法线和 edge 不严格共面）在视觉上
        // 可忽略；真正的硬边在模型上是顶点拆开的，那里两者本来就一致。
        const float det = glm::determinant(glm::mat2{in.duv1, in.duv2});
        if (std::abs(det) < consts::kMinNormalizeLength) {
            return in.color; // UV 退化，构造不出切线基
        }
        const float inv_det = 1.f / det;

        const glm::vec3 N = glm::normalize(in.normal);

        const glm::vec3 T_raw =
            (in.edge1 * in.duv2.y - in.edge2 * in.duv1.y) * inv_det;
        glm::vec3 T = T_raw - N * glm::dot(N, T_raw); // Gram-Schmidt
        const float tlen = glm::length(T);
        if (tlen < consts::kMinNormalizeLength) {
            return in.color;
        }
        T /= tlen;

        const float handed = (det < 0.f) ? -1.f : 1.f;
        const glm::vec3 B = glm::cross(N, T) * handed;
        const glm::mat3 TBN{T, B, N}; // 列 = T, B, N

        const glm::vec3 tn = normal_map
                                 ? normal_map->sample(in.texcoord) * 2.f - 1.f
                                 : glm::vec3{0.f, 0.f, 1.f};

        // 法线强度：只缩放切向分量。缩放 z 会在 normalize
        // 后被抵消，等于没效果。 strength=0 时 xy 归零 -> 归一化得到 (0,0,1) ->
        // 完全平坦
        const float k = std::max(0.f, normal_strength);
        const glm::vec3 tn_scaled =
            glm::normalize(glm::vec3{tn.x * k, tn.y * k, tn.z});

        // 切线空间 -> 相机空间
        const glm::vec3 n = glm::normalize(TBN * tn_scaled);

        const Color albedo = diffuse ? diffuse->sample(in.texcoord) : in.color;

        // 高光贴图：灰度遮罩，控制"哪里亮哪里哑"。
        // 它数值很暗（P90 只有 0.05），所以要乘一个增益才看得出铠甲的光泽。
        // 注意它只调制【高光】，不调制漫反射 —— 这是它和 diffuse 的分工。
        const float gloss =
            specular_map
                ? glm::min(1.f,
                           specular_map->sample(in.texcoord).r * specular_gain)
                : 1.f;

        // 自发光贴图：眼睛和胸口的宝石。它不参与光照计算，最后直接加上去。
        // 同样很暗（最大只有 0.38），要放大。
        const Color glow =
            glow_map ? glow_map->sample(in.texcoord) * glow_gain : Color{0.f};

        const glm::vec3 p = in.view_position; // 相机空间位置
        const glm::vec3 v = glm::normalize(-in.view_position);

        Color result = albedo * ambient;
        for (const glm::vec3 &lw : lights_world) {
            // 光源位置变到相机空间，否则和 n 不在同一空间
            const glm::vec3 lv = glm::vec3{ctx.model_view * glm::vec4{lw, 1.f}};
            const glm::vec3 l =
                glm::normalize(lv - p); // 表面 -> 光源（点光源）

            const glm::vec3 hv = l + v;
            const glm::vec3 h = glm::length(hv) > consts::kMinNormalizeLength
                                    ? glm::normalize(hv)
                                    : glm::vec3{0.f};
            const float diff = std::max(0.f, glm::dot(n, l));
            const float spec =
                std::pow(std::max(0.f, glm::dot(n, h)), shininess);

            // 漫反射用贴图色，高光用白色（强度由高光贴图调制）
            result += albedo * (diffuse_factor * diff) +
                      Color{1.f, 1.f, 1.f} * (specular * gloss * spec);
        }

        // 自发光最后叠加：不受光照影响，也不该被 N·L 调制
        result += glow;

        return glm::min(result, Color{1.f, 1.f, 1.f}); // 防止过曝
    }

    std::shared_ptr<Texture> diffuse;    ///< 漫反射贴图（反照率）
    std::shared_ptr<Texture> normal_map; ///< 切线空间法线贴图
    std::shared_ptr<Texture> specular_map; ///< 高光遮罩（灰度，调制高光强度）
    std::shared_ptr<Texture> glow_map; ///< 自发光（眼睛、胸口宝石）
    std::vector<glm::vec3> lights_world; ///< 光源位置（世界空间）
    Color ambient{0.15f};                ///< 环境光系数
    float diffuse_factor{0.45f};         ///< 漫反射系数
    float specular{0.40f};               ///< 高光系数
    float shininess{35.f};               ///< 高光指数
    float normal_strength{1.f}; ///< 法线强度（1=原始, 0=平坦）
    float specular_gain{8.f};   ///< 高光贴图的增益（它数值很暗）
    float glow_gain{3.f};       ///< 自发光增益（它数值很暗）
};

} // namespace

int main() {
    const char *ppm_path = "out.ppm";
    const char *obj_path = ASSETS "/diablo3_pose/diablo3_pose.obj";
    const char *diffuse_path = ASSETS "/diablo3_pose/diablo3_pose_diffuse.tga";
    const char *normal_path =
        ASSETS "/diablo3_pose/diablo3_pose_nm_tangent.tga";
    const char *specular_path = ASSETS "/diablo3_pose/diablo3_pose_spec.tga";
    const char *glow_path = ASSETS "/diablo3_pose/diablo3_pose_glow.tga";

    std::string error;
    auto mesh = ObjLoader::Load(obj_path, &error);
    if (!mesh) {
        std::fprintf(stderr, "load failed: %s\n", error.c_str());
        return 1;
    }
    std::fprintf(stderr,
                 "loaded: %zu vertices, %zu triangles\n",
                 mesh->vertices.size(),
                 mesh->triangle_count());

    Camera camera{};
    camera.eye = {-1.f, 0.f, 2.f};
    camera.center = {0.f, 0.f, 0.f};
    camera.up = {0.f, 1.f, 0.f};
    camera.fov = glm::radians(60.f);
    camera.n = 0.1f;
    camera.f = 100.f;
    camera.w = 800;
    camera.h = 800;

    Rasterizer r(camera);

    // 就地构造并接管所有权；emplace_shader 返回引用，方便继续设参数
    LambertTextureShader &shader = r.emplace_shader<LambertTextureShader>();

    auto diffuse = Texture::LoadTga(diffuse_path, &error);
    if (!diffuse) {
        std::fprintf(stderr, "diffuse load failed: %s\n", error.c_str());
        return 1;
    }
    auto normal_map = Texture::LoadTga(normal_path, &error);
    auto specular_map = Texture::LoadTga(specular_path, &error);
    auto glow_map = Texture::LoadTga(glow_path, &error);
    if (!normal_map || !specular_map || !glow_map) {
        std::fprintf(stderr, "texture load failed: %s\n", error.c_str());
        return 1;
    }
    std::fprintf(
        stderr,
        "textures: diffuse %dx%d, normal %dx%d, spec %dx%d, glow %dx%d\n",
        diffuse->width(),
        diffuse->height(),
        normal_map->width(),
        normal_map->height(),
        specular_map->width(),
        specular_map->height(),
        glow_map->width(),
        glow_map->height());

    shader.diffuse = std::make_shared<Texture>(std::move(*diffuse));
    shader.normal_map = std::make_shared<Texture>(std::move(*normal_map));
    shader.specular_map = std::make_shared<Texture>(std::move(*specular_map));
    shader.glow_map = std::make_shared<Texture>(std::move(*glow_map));
    shader.lights_world = {
        {2.f, 3.f, 4.f},  // 主光：右上前方
        {-3.f, 1.f, 2.f}, // 补光：左侧
    };

    // draw() 不自动清屏，必须自己调 —— 否则 zbuffer 读的是未初始化内存
    r.clear(BufferType::kColor | BufferType::kDepth);

    r.draw(*mesh);

    if (!write_ppm(ppm_path, r.buffer())) {
        return 1;
    }
    std::fprintf(stderr, "wrote %s\n", ppm_path);
    return 0;
}
