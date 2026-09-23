#pragma once

// OBJ 加载器。
//
// 对外只有一个入口 ObjLoader::Load()，返回去重后的 IndexedMesh。
// OBJ 的内部细节（三个属性池、面角下标、1 起始转换、属性缺失处理、
// 多边形三角化）全部封装在 .cpp 里，调用方看不到。
//
// 用法：
//   std::string err;
//   auto mesh = ObjLoader::Load("assets/xxx.obj", &err);
//   if (!mesh) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
//   mesh->set_color({1.f, 1.f, 1.f});   // OBJ 没有颜色信息，自己给
//   rasterizer.draw(*mesh);

#include "mesh.hpp"

#include <optional>
#include <string>
#include <string_view>

struct ObjLoader {
    /// 读 .obj，返回唯一顶点 + 三角形索引。
    ///
    /// - 顶点按 (position, normal, texcoord) 三元组去重：位置相同但法线/UV
    ///   不同的面角（硬边、纹理接缝）会保留成多个顶点，不丢失区分
    /// - 所有面都会三角化（四边面、多边形都能处理）
    /// - 缺少 vn / vt 的面角，对应字段留零（不报错）
    /// - 顶点色留空（{0,0,0}），用 IndexedMesh::set_color() 或自己填
    /// - 失败时返回 nullopt，原因写入 out_error（可为 nullptr）
    [[nodiscard]] static std::optional<IndexedMesh>
    Load(std::string_view path, std::string *out_error = nullptr);

    ObjLoader() = delete;
};
