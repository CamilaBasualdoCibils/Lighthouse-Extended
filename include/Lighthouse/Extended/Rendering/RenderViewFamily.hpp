#pragma once
#include <Lighthouse/Extended/Math/Types.hpp>
#include <cstdint>
#include <vector>

namespace Lighthouse
{
using RenderViewID = std::uint64_t;
enum class RenderViewRole { Camera, LeftEye, RightEye };
enum class RenderViewGrouping { Independent, Stereo };
struct RenderViewData
{
  RenderViewID id{}; // Stable, nonzero identity for per-view temporal state.
  Mat4 view{1.0f};
  Mat4 projection{1.0f};
  Vec3 cameraPosition{};
  UVec2 resolution{1280, 720};
  RenderViewRole role{RenderViewRole::Camera};
  Mat4 ViewProjection() const { return projection * view; }
};
struct RenderViewFamily
{
  std::vector<RenderViewData> views;
  RenderViewGrouping grouping{RenderViewGrouping::Independent};
  void Validate() const;
};
} // namespace Lighthouse
