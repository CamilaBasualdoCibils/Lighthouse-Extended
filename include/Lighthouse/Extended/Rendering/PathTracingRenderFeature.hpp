#pragma once
#include <Lighthouse/Extended/Rendering/IRenderFeature.hpp>
#include <Lighthouse/Extended/Rendering/RenderProviderOwnership.hpp>
#include <concepts>
namespace Lighthouse
{
struct RayGeometryInstance
{
  RenderObjectHandle mesh{}, material{};
  Mat4 worldTransform{1.0f};
  // Preserve the authored material/mesh definitions for the scene ray pipeline.
  ModelHandle sourceMesh;
  MaterialHandle sourceMaterial;
};
struct RaySceneData
{
  std::vector<RayGeometryInstance> instances;
  // Custom geometry/acceleration construction can publish graph dependencies.
  std::vector<RenderGraphResource> dependencies;
};
struct PathTracingRenderContext
{
  RenderFeatureContext& frame;
  RaySceneData scene;
  RenderFeatureResult output;
  std::vector<RenderGraphResource> lightSamplingData;
};

class IPathTracingProvider : public virtual IRenderProvider
{
};
class IRaySceneProvider : public virtual IPathTracingProvider
{
public:
  virtual void BuildGraph(PathTracingRenderContext&) = 0;
};
class IPathTracingIntegrator : public virtual IPathTracingProvider
{
public:
  virtual void BuildGraph(PathTracingRenderContext&) = 0;
};
class IPathTracingLightSamplingProvider : public virtual IPathTracingProvider
{
public:
  virtual void BuildGraph(PathTracingRenderContext&) = 0;
};
class IPathTracingDenoiser : public virtual IPathTracingProvider
{
public:
  virtual void BuildGraph(PathTracingRenderContext&) = 0;
};
class IPathTracingCompositeProvider : public virtual IPathTracingProvider
{
public:
  virtual void BuildGraph(PathTracingRenderContext&) = 0;
};
class IPathTracingPostProcess : public virtual IPathTracingProvider
{
public:
  virtual void BuildGraph(PathTracingRenderContext&) = 0;
};
class PathTracingRenderFeature : public IRenderFeature,
                                 protected RenderProviderOwnership
{
public:
  template <class T, class... Args>
    requires std::derived_from<T, IRaySceneProvider>
  T& AddProvider(Args&&... args)
  {
    return Add<T>(m_geometry, std::forward<Args>(args)...);
  }
  template <class T, class... Args>
    requires std::derived_from<T, IPathTracingIntegrator>
  T& SetIntegrator(Args&&... args)
  {
    return Set<T>(m_integrator, std::forward<Args>(args)...);
  }
  template <class T, class... Args>
    requires std::derived_from<T, IPathTracingLightSamplingProvider>
  T& SetLightSampling(Args&&... args)
  {
    return Set<T>(m_lightSampling, std::forward<Args>(args)...);
  }
  template <class T, class... Args>
    requires std::derived_from<T, IPathTracingDenoiser>
  T& SetDenoiser(Args&&... args)
  {
    return Set<T>(m_denoiser, std::forward<Args>(args)...);
  }
  template <class T, class... Args>
    requires std::derived_from<T, IPathTracingCompositeProvider>
  T& AddCompositor(Args&&... args)
  {
    return Add<T>(m_compositors, std::forward<Args>(args)...);
  }
  template <class T, class... Args>
    requires std::derived_from<T, IPathTracingPostProcess>
  T& AddPathTracingPostProcess(Args&&... args)
  {
    return Add<T>(m_post, std::forward<Args>(args)...);
  }
  std::uint64_t GetConfigurationRevision() const noexcept override;
  void Update(RenderUpdateContext&) override;
  RenderFeatureResult BuildGraph(RenderFeatureContext&) override;

private:
  std::vector<std::shared_ptr<IRaySceneProvider>> m_geometry;
  std::shared_ptr<IPathTracingIntegrator> m_integrator;
  std::shared_ptr<IPathTracingLightSamplingProvider> m_lightSampling;
  std::shared_ptr<IPathTracingDenoiser> m_denoiser;
  std::vector<std::shared_ptr<IPathTracingCompositeProvider>> m_compositors;
  std::vector<std::shared_ptr<IPathTracingPostProcess>> m_post;
};
} // namespace Lighthouse
