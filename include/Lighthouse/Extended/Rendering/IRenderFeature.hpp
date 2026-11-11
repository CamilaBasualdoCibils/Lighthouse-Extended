#pragma once
#include <Lighthouse/Extended/Rendering/IRenderOutput.hpp>
#include <Lighthouse/Extended/Rendering/ResourceRequests.hpp>
#include <memory>

namespace Lighthouse
{
struct RenderUpdateContext
{
  ResourceRequests& resources; // Existing stub API; unchanged by this infrastructure.
  float deltaTime{};
  std::uint64_t frameId{};
};
struct RenderFeatureContext
{
  TechniqueBuildContext& technique;
  // Live matrices/poses updated before execution. Passes may retain this pointer.
  // Changing identities, count or resolution requires graph recompilation.
  std::shared_ptr<const RenderViewFamily> family;
  RenderOutputTargets destinations;
  std::uint64_t buildId{};
  std::vector<std::shared_ptr<void>>& keepAlive;
};
struct RenderFeatureResult
{
  RenderOutputTargets color;
};
class IRenderProvider
{
public:
  virtual ~IRenderProvider() = default;
  virtual std::uint64_t GetConfigurationRevision() const noexcept { return 0; }
  virtual void Update(RenderUpdateContext&) {}
};
class IRenderFeature
{
public:
  virtual ~IRenderFeature() = default;
  virtual void Update(RenderUpdateContext&) {}
  virtual std::uint64_t GetConfigurationRevision() const noexcept { return 0; }
  virtual RenderFeatureResult BuildGraph(RenderFeatureContext&) = 0;
};

} // namespace Lighthouse
