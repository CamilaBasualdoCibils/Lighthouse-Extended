#pragma once
#include <Lighthouse/Extended/Rendering/RenderViewFamily.hpp>
#include <Lighthouse/Engine/Rendering/RenderGraphComposer.hpp>
#include <functional>
#include <optional>
#include <span>

namespace Lighthouse
{
struct RenderViewport
{
  std::uint32_t x{}, y{}, width{}, height{};
};
enum class RenderColorEncoding { SceneLinear, SRGB, DisplayLinear, BT2020PQ };
enum class RenderTargetKind { Image, ImportedImage, Window };
struct RenderOutputTarget
{
  RenderViewID view{};
  RenderGraphResource image{kInvalidGraphResource};
  std::uint32_t arrayLayer{}, mipLevel{};
  RenderViewport viewport;
  ImageFormat format{ImageFormat::RGBA8};
  RenderColorEncoding encoding{RenderColorEncoding::SRGB};
  RenderTargetKind kind{RenderTargetKind::Image};
};
using RenderOutputTargets = std::vector<RenderOutputTarget>;
void ValidateRenderTargets(const RenderViewFamily&, std::span<const RenderOutputTarget>);

// Graph-build-only targets. Presentation remains the responsibility of IOutputView.
class IRenderOutput
{
public:
  virtual ~IRenderOutput() = default;
  virtual RenderOutputTargets BuildTargets(TechniqueBuildContext&, const RenderViewFamily&) = 0;
};

// A window, including a rectangular region for split screen. One view per region.
class WindowRenderOutput final : public IRenderOutput
{
public:
  explicit WindowRenderOutput(RenderViewHandle window,
                              std::optional<RenderViewport> region = {})
      : m_window(window), m_region(region) {}
  RenderOutputTargets BuildTargets(TechniqueBuildContext&, const RenderViewFamily&) override;
private:
  RenderViewHandle m_window;
  std::optional<RenderViewport> m_region;
};

// Use a factory for offscreen/XR/custom outputs so graph handles are obtained anew
// on every build. XR acquisition, poses and submission belong to the XR integration.
class GraphRenderOutput final : public IRenderOutput
{
public:
  using Factory = std::function<RenderOutputTargets(TechniqueBuildContext&, const RenderViewFamily&)>;
  explicit GraphRenderOutput(Factory factory) : m_factory(std::move(factory))
  {
    if (!m_factory) throw std::invalid_argument("Missing output target factory");
  }
  RenderOutputTargets BuildTargets(TechniqueBuildContext& ctx, const RenderViewFamily& family) override
  { return m_factory(ctx, family); }
private:
  Factory m_factory;
};
} // namespace Lighthouse
