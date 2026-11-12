#pragma once
#include <Lighthouse/Extended/Rendering/DeferredRenderFeature.hpp>
#include <Lighthouse/Extended/Rendering/ForwardRenderFeature.hpp>
#include <Lighthouse/Extended/Resources/Image.hpp>

namespace Lighthouse
{
// This concrete adapter explicitly supports both raster pipelines. Requiring a
// sky that implements both contracts prevents accidental compatibility
// promises.
class EnvironmentProvider final : public IForwardEnvironmentProvider,
                                  public IDeferredEnvironmentProvider
{
public:
  template <class T>
    requires std::derived_from<T, IForwardSkyProvider> &&
                 std::derived_from<T, IDeferredSkyProvider>
  explicit EnvironmentProvider(std::shared_ptr<T> sky)
      : m_forwardSky(sky), m_deferredSky(std::move(sky))
  {
    if (!m_forwardSky)
      throw std::invalid_argument("Environment requires a sky provider");
  }
  IForwardSkyProvider* GetForwardSky() const override
  {
    return m_forwardSky.get();
  }
  IDeferredSkyProvider* GetDeferredSky() const override
  {
    return m_deferredSky.get();
  }
  DeferredDependencies GetDeferredDependencies() const override
  {
    return m_deferredSky->GetDeferredDependencies();
  }
  void Update(RenderUpdateContext& context) override
  {
    m_forwardSky->Update(context);
  }
  std::uint64_t GetConfigurationRevision() const noexcept override
  {
    return m_forwardSky->GetConfigurationRevision();
  }

private:
  std::shared_ptr<IForwardSkyProvider> m_forwardSky;
  std::shared_ptr<IDeferredSkyProvider> m_deferredSky;
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
  std::shared_ptr<SceneTextureGPU>
  GetPrefilteredTexture(IRenderDevice&, UVec2 resolution = {512, 256},
                        unsigned samples = 1024);
  std::shared_ptr<SceneTextureGPU> GetBRDFTexture(IRenderDevice&);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
enum class IrradianceMode
{
  Compute,
  Import
};
struct HDRIIndirectLightingOptions
{
  IrradianceMode mode = IrradianceMode::Compute;
  std::optional<Image> irradiance;
  UVec2 resolution{64, 32};
  std::uint32_t samples = 1024;
  UVec2 specularResolution{512, 256};
  std::uint32_t specularSamples = 1024;
};
// Returns physical irradiance (integral of L*cos(theta) over the hemisphere).
// Imported maps must use the same convention, linear RGB, and lat-long
// orientation.
ImageData ComputeHDRIIrradiance(const ImageData&, UVec2 resolution = {64, 32},
                                std::uint32_t samples = 1024);
class HDRISkyProvider final : public IForwardSkyProvider,
                              public IDeferredSkyProvider
{
public:
  explicit HDRISkyProvider(std::shared_ptr<HDRIEnvironment>);
  ~HDRISkyProvider();
  std::uint64_t GetConfigurationRevision() const noexcept override;
  void BuildGraphForward(ForwardRenderContext&) override;
  void BuildGraphDeferred(DeferredRenderContext&) override;

private:
  void BuildSky(RenderFeatureContext&, RenderFeatureResult&);
  std::shared_ptr<HDRIEnvironment> m_environment;
};
class HDRIIndirectLightingProvider final
    : public IForwardIndirectLightingProvider,
      public IDeferredIndirectLightingProvider
{
public:
  explicit HDRIIndirectLightingProvider(std::shared_ptr<HDRIEnvironment>,
                                        HDRIIndirectLightingOptions = {});
  ~HDRIIndirectLightingProvider();
  std::uint64_t GetConfigurationRevision() const noexcept override;
  void BuildGraph(ForwardRenderContext&) override;
  void BuildGraph(DeferredRenderContext&) override;
  DeferredDependencies GetDeferredDependencies() const override;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
} // namespace Lighthouse
