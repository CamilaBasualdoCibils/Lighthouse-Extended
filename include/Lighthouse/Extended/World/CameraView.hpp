#pragma once
#include <Lighthouse/Extended/Rendering/RenderViewFamily.hpp>
#include <stdexcept>

namespace Lighthouse
{
// Scene camera settings only. RenderFeatureView owns features, outputs and post-processing.
class CameraView
{
public:
  void SetResolution(UVec2 size)
  {
    if (!size.x || !size.y) throw std::invalid_argument("Camera resolution must be nonzero");
    m_resolution = size;
  }
  UVec2 GetResolution() const noexcept { return m_resolution; }
  float verticalFov = 45.0f;
  float nearPlane = 0.1f;
  float farPlane = 100.0f;
private:
  UVec2 m_resolution{1280, 720};
};
} // namespace Lighthouse
