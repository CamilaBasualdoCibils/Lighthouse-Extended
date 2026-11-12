#pragma once
#include <Lighthouse/Extended/Rendering/ForwardRenderFeature.hpp>
#include <Lighthouse/Extended/Rendering/DeferredRenderFeature.hpp>
namespace Lighthouse
{
struct BloomSettings
{
  bool Enabled{true};
  float Threshold{1.0f};
  float SoftKnee{0.5f};
  float Intensity{0.15f};
  std::uint32_t MipCount{5};
};
class Bloom final : public IForwardPostProcess, public IDeferredPostProcess
{
public:
  explicit Bloom(BloomSettings = {});
  const BloomSettings& GetSettings() const noexcept;
  void SetSettings(BloomSettings);
  std::uint64_t GetConfigurationRevision() const noexcept override;
  void BuildGraphForward(ForwardRenderContext&) override;
  void BuildGraphDeferred(DeferredRenderContext&) override;

private:
  void BuildBloom(RenderFeatureContext&, RenderFeatureResult&);
  std::shared_ptr<BloomSettings> m_settings;
};
static_assert(std::derived_from<Bloom, IForwardPostProcess>);
static_assert(std::derived_from<Bloom, IDeferredPostProcess>);
static_assert(std::derived_from<Bloom, IRenderProvider>);
} // namespace Lighthouse
