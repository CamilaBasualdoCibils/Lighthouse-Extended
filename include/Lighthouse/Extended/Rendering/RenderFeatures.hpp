#pragma once
#include <Lighthouse/Extended/Rendering/IRenderFeature.hpp>
#include <Lighthouse/Extended/Rendering/RenderProviderOwnership.hpp>
#include <Lighthouse/Extended/Rendering/GBufferResources.hpp>
#include <concepts>
#include <map>

namespace Lighthouse
{
enum class ForwardSemantics { Depth };
enum class DeferredSemantics { Depth, Normal, Motion, Albedo, Metallic, Roughness, WorldPosition, Emission, SurfaceProperties };
enum class DeferredHistory { Color, Depth, Motion };
struct DeferredDependencies
{
  std::vector<DeferredSemantics> attachments;
  std::vector<DeferredHistory> history;
};
struct ForwardRenderResult : RenderFeatureResult
{
  std::map<ForwardSemantics, RenderOutputTargets> auxiliary;
};
struct DeferredRenderResult : RenderFeatureResult
{
  std::map<DeferredSemantics, RenderOutputTargets> auxiliary;
  std::map<DeferredHistory, RenderOutputTargets> history;
};
class IForwardProvider : public virtual IRenderProvider {};
class IDeferredProvider : public virtual IRenderProvider
{
public:
  virtual DeferredDependencies GetDeferredDependencies() const { return {}; }
};
struct DeferredRenderContext
{
  RenderFeatureContext& frame;
  std::vector<GBufferResources> gbuffers; // One per logical view; array images can be shared.
  DeferredRenderResult output;
  std::vector<RenderGraphResource> lights, shadows, reflections;
};
class ForwardRenderServices;
struct ForwardRenderContext
{
  RenderFeatureContext& frame;
  ForwardRenderResult output;
  std::vector<RenderGraphResource> lights, shadows, reflections, lightAssignments;
  // Incident-light shader declares set 2/binding 0; surface response belongs to Materials.
  std::string lightingShader;
  BufferHandle lightingBuffer{};
  std::string indirectShader;
  DescriptorSetLayoutHandle indirectLayout{};
  DescriptorSetHandle indirectSet{};
  std::shared_ptr<ForwardRenderServices> rendering;
};
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

#define LIGHTHOUSE_RENDER_PROVIDER(Name, Context, Base) \
class Name : public virtual Base { \
public: virtual void BuildGraph(Context&) = 0; \
}
LIGHTHOUSE_RENDER_PROVIDER(IDeferredPreparationProvider, DeferredRenderContext, IDeferredProvider);
LIGHTHOUSE_RENDER_PROVIDER(IDeferredGeometryProvider, DeferredRenderContext, IDeferredProvider);
LIGHTHOUSE_RENDER_PROVIDER(IDeferredLightingProvider, DeferredRenderContext, IDeferredProvider);
LIGHTHOUSE_RENDER_PROVIDER(IDeferredShadowProvider, DeferredRenderContext, IDeferredProvider);
LIGHTHOUSE_RENDER_PROVIDER(IDeferredReflectionProvider, DeferredRenderContext, IDeferredProvider);
LIGHTHOUSE_RENDER_PROVIDER(IDeferredCompositeProvider, DeferredRenderContext, IDeferredProvider);
class IDeferredPostProcess : public virtual IDeferredProvider
{
public: virtual void BuildGraphDeferred(DeferredRenderContext&) = 0;
};
LIGHTHOUSE_RENDER_PROVIDER(IForwardGeometryProvider, ForwardRenderContext, IForwardProvider);
class IForwardLightingProvider : public virtual IForwardProvider
{
public:
  // Publish graph dependencies, a storage buffer at set 2/binding 0, and GLSL
  // implementing ForwardLightCount/ForwardLightAt/ForwardAmbientRadiance.
  // Geometry providers consume feature-created pipelines; they do not shade lights.
  virtual void BuildGraph(ForwardRenderContext&) = 0;
};
// Indirect shaders expose ForwardDiffuseIrradiance(position, normal) and
// ForwardEnvironmentRadiance(position, direction, roughness) plus
// ForwardEnvironmentBRDF(nv, roughness), with resources at set 3.
LIGHTHOUSE_RENDER_PROVIDER(IIndirectLightingProvider, ForwardRenderContext, IForwardProvider);
LIGHTHOUSE_RENDER_PROVIDER(IForwardShadowProvider, ForwardRenderContext, IForwardProvider);
LIGHTHOUSE_RENDER_PROVIDER(IForwardReflectionProvider, ForwardRenderContext, IForwardProvider);
LIGHTHOUSE_RENDER_PROVIDER(IForwardLightAssignmentProvider, ForwardRenderContext, IForwardProvider);
LIGHTHOUSE_RENDER_PROVIDER(IForwardCompositeProvider, ForwardRenderContext, IForwardProvider);
class IForwardPostProcess : public virtual IForwardProvider
{
public: virtual void BuildGraphForward(ForwardRenderContext&) = 0;
};
LIGHTHOUSE_RENDER_PROVIDER(IForwardPreparationProvider, ForwardRenderContext, IForwardProvider);
LIGHTHOUSE_RENDER_PROVIDER(IRaySceneProvider, PathTracingRenderContext, IRenderProvider);
LIGHTHOUSE_RENDER_PROVIDER(IPathTracingIntegrator, PathTracingRenderContext, IRenderProvider);
LIGHTHOUSE_RENDER_PROVIDER(IPathTracingLightSamplingProvider, PathTracingRenderContext, IRenderProvider);
LIGHTHOUSE_RENDER_PROVIDER(IPathTracingDenoiser, PathTracingRenderContext, IRenderProvider);
LIGHTHOUSE_RENDER_PROVIDER(IPathTracingCompositeProvider, PathTracingRenderContext, IRenderProvider);
LIGHTHOUSE_RENDER_PROVIDER(IPathTracingPostProcess, PathTracingRenderContext, IRenderProvider);
#undef LIGHTHOUSE_RENDER_PROVIDER

// Features orchestrate providers. Forward uses the shared material system; deferred
// and path-tracing rendering algorithms remain scaffolds.
class DeferredRenderFeature : public IRenderFeature, protected RenderProviderOwnership
{
public:
  template<class T, class... Args> requires std::derived_from<T, IDeferredPreparationProvider>
  T& AddDeferredPreparationProvider(Args&&... args) { return Add<T>(m_preparation, std::forward<Args>(args)...); }
  template<class T, class... Args> requires std::derived_from<T, IDeferredGeometryProvider>
  T& AddProvider(Args&&... args) { return Add<T>(m_geometry, std::forward<Args>(args)...); }
  template<class T, class... Args> requires std::derived_from<T, IDeferredLightingProvider>
  T& SetLighting(Args&&... args) { return Set<T>(m_lighting, std::forward<Args>(args)...); }
  template<class T, class... Args> requires std::derived_from<T, IDeferredShadowProvider>
  T& SetShadows(Args&&... args) { return Set<T>(m_shadows, std::forward<Args>(args)...); }
  template<class T, class... Args> requires std::derived_from<T, IDeferredReflectionProvider>
  T& SetReflections(Args&&... args) { return Set<T>(m_reflections, std::forward<Args>(args)...); }
  template<class T, class... Args> requires std::derived_from<T, IDeferredCompositeProvider>
  T& AddCompositor(Args&&... args) { return Add<T>(m_compositors, std::forward<Args>(args)...); }
  template<class T, class... Args> requires std::derived_from<T, IDeferredPostProcess>
  T& AddDeferredPostProcess(Args&&... args) { return Add<T>(m_post, std::forward<Args>(args)...); }
  std::uint64_t GetConfigurationRevision() const noexcept override;
  void Update(RenderUpdateContext&) override;
  RenderFeatureResult BuildGraph(RenderFeatureContext&) override;
private:
  std::vector<std::shared_ptr<IDeferredPreparationProvider>> m_preparation;
  std::vector<std::shared_ptr<IDeferredGeometryProvider>> m_geometry;
  std::shared_ptr<IDeferredLightingProvider> m_lighting;
  std::shared_ptr<IDeferredShadowProvider> m_shadows;
  std::shared_ptr<IDeferredReflectionProvider> m_reflections;
  std::vector<std::shared_ptr<IDeferredCompositeProvider>> m_compositors;
  std::vector<std::shared_ptr<IDeferredPostProcess>> m_post;
};
class IEnvironmentProvider;
class ForwardRenderFeature : public IRenderFeature, protected RenderProviderOwnership
{
public:
  template<class T, class... Args> requires std::derived_from<T, IForwardPreparationProvider>
  T& AddForwardPreparationProvider(Args&&... args) { return Add<T>(m_preparation, std::forward<Args>(args)...); }
  template<class T, class... Args> requires std::derived_from<T, IEnvironmentProvider>
  T& SetEnvironmentProvider(Args&&... args) { return Set<T>(m_environment, std::forward<Args>(args)...); }
  template<class T, class... Args> requires std::derived_from<T, IIndirectLightingProvider>
  T& SetIndirectLightingProvider(Args&&... args) { return Set<T>(m_indirect, std::forward<Args>(args)...); }
  template<class T, class... Args> requires std::derived_from<T, IForwardGeometryProvider>
  T& AddProvider(Args&&... args) { return Add<T>(m_geometry, std::forward<Args>(args)...); }
  template<class T, class... Args> requires std::derived_from<T, IForwardLightingProvider>
  T& SetLighting(Args&&... args) { return Set<T>(m_lighting, std::forward<Args>(args)...); }
  template<class T, class... Args> requires std::derived_from<T, IForwardShadowProvider>
  T& SetShadows(Args&&... args) { return Set<T>(m_shadows, std::forward<Args>(args)...); }
  template<class T, class... Args> requires std::derived_from<T, IForwardReflectionProvider>
  T& SetReflections(Args&&... args) { return Set<T>(m_reflections, std::forward<Args>(args)...); }
  template<class T, class... Args> requires std::derived_from<T, IForwardCompositeProvider>
  T& AddCompositor(Args&&... args) { return Add<T>(m_compositors, std::forward<Args>(args)...); }
  template<class T, class... Args> requires std::derived_from<T, IForwardPostProcess>
  T& AddForwardPostProcess(Args&&... args) { return Add<T>(m_post, std::forward<Args>(args)...); }
  std::uint64_t GetConfigurationRevision() const noexcept override;
  void Update(RenderUpdateContext&) override;
  RenderFeatureResult BuildGraph(RenderFeatureContext&) override;
protected:
  void ResolveDefault(RenderFeatureContext&, RenderFeatureResult&);
  virtual void BuildLightAssignment(ForwardRenderContext&) {}
private:
  std::vector<std::shared_ptr<IForwardPreparationProvider>> m_preparation;
  std::shared_ptr<ForwardRenderServices> m_rendering;
  std::shared_ptr<IEnvironmentProvider> m_environment;
  std::shared_ptr<IIndirectLightingProvider> m_indirect;
  std::vector<std::shared_ptr<IForwardGeometryProvider>> m_geometry;
  std::shared_ptr<IForwardLightingProvider> m_lighting;
  std::shared_ptr<IForwardShadowProvider> m_shadows;
  std::shared_ptr<IForwardReflectionProvider> m_reflections;
  std::vector<std::shared_ptr<IForwardCompositeProvider>> m_compositors;
  std::vector<std::shared_ptr<IForwardPostProcess>> m_post;
};
class ForwardPlusRenderFeature : public ForwardRenderFeature
{
public:
  template<class T, class... Args> requires std::derived_from<T, IForwardLightAssignmentProvider>
  T& SetLightAssignment(Args&&... args) { return Set<T>(m_assignment, std::forward<Args>(args)...); }
  std::uint64_t GetConfigurationRevision() const noexcept override;
  void Update(RenderUpdateContext&) override;
protected:
  void BuildLightAssignment(ForwardRenderContext&) override;
private:
  std::shared_ptr<IForwardLightAssignmentProvider> m_assignment;
};
class PathTracingRenderFeature : public IRenderFeature, protected RenderProviderOwnership
{
public:
  template<class T, class... Args> requires std::derived_from<T, IRaySceneProvider>
  T& AddProvider(Args&&... args) { return Add<T>(m_geometry, std::forward<Args>(args)...); }
  template<class T, class... Args> requires std::derived_from<T, IPathTracingIntegrator>
  T& SetIntegrator(Args&&... args) { return Set<T>(m_integrator, std::forward<Args>(args)...); }
  template<class T, class... Args> requires std::derived_from<T, IPathTracingLightSamplingProvider>
  T& SetLightSampling(Args&&... args) { return Set<T>(m_lightSampling, std::forward<Args>(args)...); }
  template<class T, class... Args> requires std::derived_from<T, IPathTracingDenoiser>
  T& SetDenoiser(Args&&... args) { return Set<T>(m_denoiser, std::forward<Args>(args)...); }
  template<class T, class... Args> requires std::derived_from<T, IPathTracingCompositeProvider>
  T& AddCompositor(Args&&... args) { return Add<T>(m_compositors, std::forward<Args>(args)...); }
  template<class T, class... Args> requires std::derived_from<T, IPathTracingPostProcess>
  T& AddPathTracingPostProcess(Args&&... args) { return Add<T>(m_post, std::forward<Args>(args)...); }
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
