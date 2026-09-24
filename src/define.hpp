#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/ext.hpp>
#include <glm/glm.hpp>

using Color = glm::vec3;

/// 全项目的数值常量。用 inline constexpr（C++17）而不是 static constexpr ——
/// 在头文件里 inline 保证全程序一份定义。
namespace consts {

/// 归一化前的长度下限，用来避免除零得到的 NaN。
inline constexpr const float kMinNormalizeLength = 1e-6f;

/// 亚像素三角形剔除阈值。
///
/// det(ABC) 是屏幕空间三角形面积的 2 倍，所以 1.0 约等于"半个像素"。
/// 小于它的三角形不画：既丢掉退化的，也顺手丢掉背面（背面 det 为负）。
inline constexpr const float kMinTriangleArea2 = 1.f;

/// 近平面裁剪的阈值（裁剪空间 w 的下界）。
///
/// 裁剪必须在【裁剪空间】做，不能用相机空间 z：顶点跑到相机后方时
/// clip.w <= 0，透视除法会把坐标翻转并放大到无穷，产生横跨屏幕的假三角形。
/// w 必须严格大于 0 才能安全地做除法。
inline constexpr float kNearPlaneEps = 1e-4f;

/// 每个像素的子样本偏移（SSAA）。
///
/// 2x2 网格，坐标在每个像素内取 [0,1]，所以是 (0.25, 0.75) 四个组合。
/// 顺序无所谓 —— 下面只是加权平均。
inline constexpr int kSubSampleCount = 4;
inline constexpr float kSubSampleOffset[4][2] = {
    {0.375f, 0.125f}, {0.875f, 0.375f}, {0.125f, 0.625f}, {0.625f, 0.875f}};

} // namespace consts
