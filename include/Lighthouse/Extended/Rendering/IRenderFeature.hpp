#pragma once
#include <Lighthouse/Extended/Rendering/IRenderOutput.hpp>
#include <Lighthouse/Extended/Rendering/ResourceRequests.hpp>
#include <map>
#include <memory>
#include <concepts>

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
enum class RenderSemantic { Depth, WorldNormal, Motion, Albedo, Roughness, Variance, SampleCount, HitDistance };
struct RenderFeatureResult
{
  // Current graph versions, in family order. Intermediate HDR color may differ
  // from the final destinations. A feature's resolver performs any required conversion.
  RenderOutputTargets color;
  std::map<RenderSemantic, RenderOutputTargets> auxiliary;
};
class IRenderProvider
{
public:
  virtual ~IRenderProvider() = default;
  virtual std::uint64_t GetConfigurationRevision() const noexcept { return 0; }
  virtual void Update(RenderUpdateContext&) {}
};
class IRenderPreparationProvider : public IRenderProvider
{
public:
  virtual void BuildGraph(RenderFeatureContext&) = 0;
};
class IRenderPostProcess : public IRenderProvider
{
public:
  virtual ~IRenderPostProcess() = default;
  virtual std::vector<RenderSemantic> RequiredInputs() const { return {}; }
  virtual void BuildGraph(RenderFeatureContext&, RenderFeatureResult&) = 0;
};
class IRenderOutputResolver : public IRenderProvider
{
public:
  virtual ~IRenderOutputResolver() = default;
  // Resolve/encode/composite to destinations and return their new graph versions.
  virtual RenderOutputTargets BuildGraph(RenderFeatureContext&, const RenderFeatureResult&) = 0;
};
class IRenderFeature
{
public:
  virtual ~IRenderFeature() = default;
  virtual void Update(RenderUpdateContext&) {}
  virtual std::uint64_t GetConfigurationRevision() const noexcept { return 0; }
  virtual RenderFeatureResult BuildGraph(RenderFeatureContext&) = 0;
};

// Typed ownership utilities used by feature implementations. Batching is provider-owned.
class RenderFeatureBase : public IRenderFeature
{
public:
  template<class T, class... Args> requires std::derived_from<T, IRenderPreparationProvider>
  T& AddPreparationProvider(Args&&... args)
  { return Add<T>(m_preparation, std::forward<Args>(args)...); }
  template<class T, class... Args> requires std::derived_from<T, IRenderPostProcess>
  T& AddPostProcess(Args&&... args)
  { return Add<T>(m_post, std::forward<Args>(args)...); }
  template<class T, class... Args> requires std::derived_from<T, IRenderOutputResolver>
  T& SetOutputResolver(Args&&... args)
  { return Set<T>(m_resolver, std::forward<Args>(args)...); }
  void Update(RenderUpdateContext&) override;
  std::uint64_t GetConfigurationRevision() const noexcept override { return m_revision; }
protected:
  template<class T, class Base, class... Args>
  T& Add(std::vector<std::shared_ptr<Base>>& list, Args&&... args)
  {
    auto value = std::make_shared<T>(std::forward<Args>(args)...);
    auto& result = *value;
    list.push_back(std::move(value));
    ++m_revision;
    return result;
  }
  template<class T, class Base, class... Args>
  T& Set(std::shared_ptr<Base>& slot, Args&&... args)
  {
    auto value = std::make_shared<T>(std::forward<Args>(args)...);
    auto& result = *value;
    slot = std::move(value);
    ++m_revision;
    return result;
  }
  template<class T> static void Hold(RenderFeatureContext& context, const std::shared_ptr<T>& value)
  { if (value) context.keepAlive.push_back(value); }
  void Prepare(RenderFeatureContext&);
  void Finish(RenderFeatureContext&, RenderFeatureResult&);
  virtual void ResolveDefault(RenderFeatureContext&, RenderFeatureResult&) {}
private:
  std::uint64_t m_revision{};
  std::vector<std::shared_ptr<IRenderPreparationProvider>> m_preparation;
  std::vector<std::shared_ptr<IRenderPostProcess>> m_post;
  std::shared_ptr<IRenderOutputResolver> m_resolver;
};
} // namespace Lighthouse
