#pragma once
#include <Lighthouse/Extended/Rendering/RenderFeatures.hpp>
#include <Lighthouse/Extended/Resources/Image.hpp>

namespace Lighthouse
{
// Sky has its own pipeline and accepts ordinary feature outputs, so other
// render paths can reuse it without adopting the forward material interface.
class ISkyProvider : public IRenderProvider
{
public:
  virtual void BuildGraph(RenderFeatureContext&, RenderFeatureResult&) = 0;
};
class IEnvironmentProvider : public IRenderProvider
{
public:
  virtual ISkyProvider* GetSky() const = 0;
};
class EnvironmentProvider final : public IEnvironmentProvider
{
public:
  explicit EnvironmentProvider(std::shared_ptr<ISkyProvider> sky) : m_sky(std::move(sky)) {}
  ISkyProvider* GetSky() const override { return m_sky.get(); }
  void Update(RenderUpdateContext& ctx) override { if (m_sky) m_sky->Update(ctx); }
  std::uint64_t GetConfigurationRevision() const noexcept override {
    return m_sky ? m_sky->GetConfigurationRevision() : 0;
  }
private:
  std::shared_ptr<ISkyProvider> m_sky;
};
struct SceneTextureGPU;
// Shared, lazy linear equirectangular HDRI. No file is opened at construction.
// The render device must outlive all graph/provider resources using it.
class HDRIEnvironment
{
public:
  explicit HDRIEnvironment(Image source);
  ~HDRIEnvironment();
  std::uint64_t GetRevision() const noexcept;
  std::shared_ptr<const ImageData> Resolve() const;
  std::shared_ptr<SceneTextureGPU> GetTexture(IRenderDevice&);
  std::shared_ptr<SceneTextureGPU> GetPrefilteredTexture(IRenderDevice&, UVec2 resolution={512,256},
                                                       unsigned samples=1024);
  std::shared_ptr<SceneTextureGPU> GetBRDFTexture(IRenderDevice&);
private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
enum class IrradianceMode { Compute, Import };
struct HDRIIndirectLightingOptions
{
  IrradianceMode mode = IrradianceMode::Compute;
  std::optional<Image> irradiance;
  UVec2 resolution{64, 32};
  std::uint32_t samples = 1024;
  UVec2 specularResolution{512,256};
  std::uint32_t specularSamples = 1024;
};
// Returns physical irradiance (integral of L*cos(theta) over the hemisphere).
// Imported maps must use the same convention, linear RGB, and lat-long orientation.
ImageData ComputeHDRIIrradiance(const ImageData&, UVec2 resolution = {64, 32},
                                std::uint32_t samples = 1024);
class HDRISkyProvider final : public ISkyProvider
{
public:
  explicit HDRISkyProvider(std::shared_ptr<HDRIEnvironment>);
  ~HDRISkyProvider();
  std::uint64_t GetConfigurationRevision() const noexcept override;
  void BuildGraph(RenderFeatureContext&, RenderFeatureResult&) override;
private:
  std::shared_ptr<HDRIEnvironment> m_environment;
};
class HDRIIndirectLightingProvider final : public IIndirectLightingProvider
{
public:
  explicit HDRIIndirectLightingProvider(std::shared_ptr<HDRIEnvironment>,
                                        HDRIIndirectLightingOptions = {});
  ~HDRIIndirectLightingProvider();
  std::uint64_t GetConfigurationRevision() const noexcept override;
  void BuildGraph(ForwardRenderContext&) override;
private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
} // namespace Lighthouse
