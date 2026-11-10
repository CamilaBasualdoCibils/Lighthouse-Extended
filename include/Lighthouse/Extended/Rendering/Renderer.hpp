#pragma once
#include <Lighthouse/Extended/Rendering/RenderFeatures.hpp>
#include <functional>
#include <memory>

namespace Lighthouse
{
class RenderFeatureView
{
public:
  using ViewSource = std::function<RenderViewFamily()>;
  explicit RenderFeatureView(std::shared_ptr<IRenderOutput> output);
  template<class T, class... Args> requires std::derived_from<T, IRenderFeature>
  T& SetRenderFeature(Args&&... args)
  {
    auto feature = std::make_shared<T>(std::forward<Args>(args)...);
    auto& result = *feature;
    m_feature = std::move(feature);
    Invalidate();
    return result;
  }
  void SetOutput(std::shared_ptr<IRenderOutput> output);
  void SetViewFamily(RenderViewFamily family);
  void SetViewSource(ViewSource source);
  void Invalidate() noexcept { ++m_revision; m_result = {}; }
  const RenderViewFamily& GetViewFamily() const noexcept { return *m_family; }
  const RenderFeatureResult& GetResult() const noexcept { return m_result; }
  bool NeedsRebuild() const noexcept;
private:
  friend class Renderer;
  void Refresh();
  std::shared_ptr<IRenderOutput> m_output;
  std::shared_ptr<IRenderFeature> m_feature;
  std::shared_ptr<RenderViewFamily> m_family{std::make_shared<RenderViewFamily>()};
  ViewSource m_source;
  std::uint64_t m_revision{1}, m_compiledRevision{}, m_compiledFeatureRevision{};
  RenderFeatureResult m_result;
};

// Owns independent view/feature configurations, not scene batches or render algorithms.
// IOutputView still owns acquisition and presentation in RenderGraphComposer.
class Renderer final : public IRenderTechnique
{
public:
  explicit Renderer(RenderGraphComposer* composer);
  ~Renderer() override;
  RenderFeatureView& CreateView(std::shared_ptr<IRenderOutput> output);
  bool RemoveView(RenderFeatureView& view);
  void Update(float deltaTime);
  bool NeedsRebuild() const noexcept;
  void BuildGraph(TechniqueBuildContext&) override;
private:
  RenderGraphComposer* m_composer;
  ResourceRequests m_resources;
  std::vector<std::shared_ptr<RenderFeatureView>> m_views;
  std::uint64_t m_frameId{}, m_buildId{};
  bool m_topologyChanged{true};
};
} // namespace Lighthouse
