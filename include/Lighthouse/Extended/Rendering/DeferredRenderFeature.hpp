#pragma once
#include <Lighthouse/Extended/Rendering/GBufferResources.hpp>
#include <Lighthouse/Extended/Rendering/IRenderFeature.hpp>
#include <Lighthouse/Extended/Rendering/RenderProviderOwnership.hpp>
#include <concepts>
#include <map>
namespace Lighthouse
{
enum class DeferredSemantics
{
  Depth,
  Normal,
  Motion,
  Albedo,
  Metallic,
  Roughness,
  WorldPosition,
  Emission,
  SurfaceProperties
};
enum class DeferredHistory
{
  Color,
  Depth,
  Motion
};
struct DeferredDependencies
{
  std::vector<DeferredSemantics> attachments;
  std::vector<DeferredHistory> history;
};
struct DeferredRenderResult : RenderFeatureResult
{
  std::map<DeferredSemantics, RenderOutputTargets> auxiliary;
  std::map<DeferredHistory, RenderOutputTargets> history;
};
class IDeferredProvider : public virtual IRenderProvider
{
public:
  virtual DeferredDependencies GetDeferredDependencies() const
  {
    return {};
  }
};
class DeferredRenderServices;
struct DeferredRenderContext
{
  RenderFeatureContext& frame;
  std::vector<GBufferResources>
      gbuffers; // One per logical view; array images can be shared.
  DeferredRenderResult output;
  std::vector<RenderGraphResource> lights, shadows, reflections;
  DeferredDependencies requirements;
  std::string indirectShader;
  DescriptorSetLayoutHandle indirectLayout{};
  DescriptorSetHandle indirectSet{};
  std::shared_ptr<DeferredRenderServices> rendering;
};
class IDeferredPreparationProvider : public virtual IDeferredProvider
{
public:
  virtual void BuildGraph(DeferredRenderContext&) = 0;
};
class IDeferredGeometryProvider : public virtual IDeferredProvider
{
public:
  virtual void BuildGraph(DeferredRenderContext&) = 0;
};
class IDeferredLightingProvider : public virtual IDeferredProvider
{
public:
  virtual void BuildGraph(DeferredRenderContext&) = 0;
};
class IDeferredIndirectLightingProvider : public virtual IDeferredProvider
{
public:
  virtual void BuildGraph(DeferredRenderContext&) = 0;
};
class IDeferredShadowProvider : public virtual IDeferredProvider
{
public:
  virtual void BuildGraph(DeferredRenderContext&) = 0;
};
class IDeferredReflectionProvider : public virtual IDeferredProvider
{
public:
  virtual void BuildGraph(DeferredRenderContext&) = 0;
};
class IDeferredCompositeProvider : public virtual IDeferredProvider
{
public:
  virtual void BuildGraph(DeferredRenderContext&) = 0;
};
class IDeferredPostProcess : public virtual IDeferredProvider
{
public:
  virtual void BuildGraphDeferred(DeferredRenderContext&) = 0;
};
class IDeferredSkyProvider : public virtual IDeferredProvider
{
public:
  virtual void BuildGraphDeferred(DeferredRenderContext&) = 0;
};
class IDeferredEnvironmentProvider : public virtual IDeferredProvider
{
public:
  virtual IDeferredSkyProvider* GetDeferredSky() const = 0;
};
class DeferredRenderFeature : public IRenderFeature,
                              protected RenderProviderOwnership
{
public:
  template <class T, class... Args>
    requires std::derived_from<T, IDeferredEnvironmentProvider>
  T& SetEnvironmentProvider(Args&&... args)
  {
    return Set<T>(m_environment, std::forward<Args>(args)...);
  }
  template <class T, class... Args>
    requires std::derived_from<T, IDeferredIndirectLightingProvider>
  T& SetIndirectLightingProvider(Args&&... args)
  {
    return Set<T>(m_indirect, std::forward<Args>(args)...);
  }
  template <class T, class... Args>
    requires std::derived_from<T, IDeferredPreparationProvider>
  T& AddDeferredPreparationProvider(Args&&... args)
  {
    return Add<T>(m_preparation, std::forward<Args>(args)...);
  }
  template <class T, class... Args>
    requires std::derived_from<T, IDeferredGeometryProvider>
  T& AddProvider(Args&&... args)
  {
    return Add<T>(m_geometry, std::forward<Args>(args)...);
  }
  template <class T, class... Args>
    requires std::derived_from<T, IDeferredLightingProvider>
  T& SetLighting(Args&&... args)
  {
    return Set<T>(m_lighting, std::forward<Args>(args)...);
  }
  template <class T, class... Args>
    requires std::derived_from<T, IDeferredShadowProvider>
  T& SetShadows(Args&&... args)
  {
    return Set<T>(m_shadows, std::forward<Args>(args)...);
  }
  template <class T, class... Args>
    requires std::derived_from<T, IDeferredReflectionProvider>
  T& SetReflections(Args&&... args)
  {
    return Set<T>(m_reflections, std::forward<Args>(args)...);
  }
  template <class T, class... Args>
    requires std::derived_from<T, IDeferredCompositeProvider>
  T& AddCompositor(Args&&... args)
  {
    return Add<T>(m_compositors, std::forward<Args>(args)...);
  }
  template <class T, class... Args>
    requires std::derived_from<T, IDeferredPostProcess>
  T& AddDeferredPostProcess(Args&&... args)
  {
    return Add<T>(m_post, std::forward<Args>(args)...);
  }
  std::uint64_t GetConfigurationRevision() const noexcept override;
  void Update(RenderUpdateContext&) override;
  RenderFeatureResult BuildGraph(RenderFeatureContext&) override;

private:
  std::shared_ptr<IDeferredEnvironmentProvider> m_environment;
  std::shared_ptr<IDeferredIndirectLightingProvider> m_indirect;
  std::vector<std::shared_ptr<IDeferredPreparationProvider>> m_preparation;
  std::vector<std::shared_ptr<IDeferredGeometryProvider>> m_geometry;
  std::shared_ptr<IDeferredLightingProvider> m_lighting;
  std::shared_ptr<IDeferredShadowProvider> m_shadows;
  std::shared_ptr<IDeferredReflectionProvider> m_reflections;
  std::vector<std::shared_ptr<IDeferredCompositeProvider>> m_compositors;
  std::vector<std::shared_ptr<IDeferredPostProcess>> m_post;
};
} // namespace Lighthouse
