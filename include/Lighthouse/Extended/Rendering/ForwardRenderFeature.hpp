#pragma once
#include <Lighthouse/Extended/Rendering/IRenderFeature.hpp>
#include <Lighthouse/Extended/Rendering/RenderProviderOwnership.hpp>
#include <concepts>
#include <map>
namespace Lighthouse
{
enum class ForwardSemantics
{
  Depth
};
struct ForwardRenderResult : RenderFeatureResult
{
  std::map<ForwardSemantics, RenderOutputTargets> auxiliary;
};
class IForwardProvider : public virtual IRenderProvider
{
};
class ForwardRenderServices;
struct ForwardRenderContext
{
  RenderFeatureContext& frame;
  ForwardRenderResult output;
  std::vector<RenderGraphResource> lights, shadows, reflections,
      lightAssignments;
  // Incident-light shader declares set 2/binding 0; surface response belongs to
  // Materials.
  std::string lightingShader;
  BufferHandle lightingBuffer{};
  std::string indirectShader;
  DescriptorSetLayoutHandle indirectLayout{};
  DescriptorSetHandle indirectSet{};
  std::shared_ptr<ForwardRenderServices> rendering;
};
class IForwardPreparationProvider : public virtual IForwardProvider
{
public:
  virtual void BuildGraph(ForwardRenderContext&) = 0;
};
class IForwardGeometryProvider : public virtual IForwardProvider
{
public:
  virtual void BuildGraph(ForwardRenderContext&) = 0;
};
// Supplies incident-light data at set 2/binding 0 and ForwardLightCount,
// ForwardLightAt and ForwardAmbientRadiance GLSL functions.
class IForwardLightingProvider : public virtual IForwardProvider
{
public:
  virtual void BuildGraph(ForwardRenderContext&) = 0;
};
// Supplies set-3 bindings plus ForwardDiffuseIrradiance,
// ForwardEnvironmentRadiance and ForwardEnvironmentBRDF material functions.
class IForwardIndirectLightingProvider : public virtual IForwardProvider
{
public:
  virtual void BuildGraph(ForwardRenderContext&) = 0;
};
class IForwardShadowProvider : public virtual IForwardProvider
{
public:
  virtual void BuildGraph(ForwardRenderContext&) = 0;
};
class IForwardReflectionProvider : public virtual IForwardProvider
{
public:
  virtual void BuildGraph(ForwardRenderContext&) = 0;
};
class IForwardLightAssignmentProvider : public virtual IForwardProvider
{
public:
  virtual void BuildGraph(ForwardRenderContext&) = 0;
};
class IForwardCompositeProvider : public virtual IForwardProvider
{
public:
  virtual void BuildGraph(ForwardRenderContext&) = 0;
};
class IForwardPostProcess : public virtual IForwardProvider
{
public:
  virtual void BuildGraphForward(ForwardRenderContext&) = 0;
};
class IForwardSkyProvider : public virtual IForwardProvider
{
public:
  virtual void BuildGraphForward(ForwardRenderContext&) = 0;
};
class IForwardEnvironmentProvider : public virtual IForwardProvider
{
public:
  virtual IForwardSkyProvider* GetForwardSky() const = 0;
};
class ForwardRenderFeature : public IRenderFeature,
                             protected RenderProviderOwnership
{
public:
  template <class T, class... Args>
    requires std::derived_from<T, IForwardPreparationProvider>
  T& AddForwardPreparationProvider(Args&&... args)
  {
    return Add<T>(m_preparation, std::forward<Args>(args)...);
  }
  template <class T, class... Args>
    requires std::derived_from<T, IForwardEnvironmentProvider>
  T& SetEnvironmentProvider(Args&&... args)
  {
    return Set<T>(m_environment, std::forward<Args>(args)...);
  }
  template <class T, class... Args>
    requires std::derived_from<T, IForwardIndirectLightingProvider>
  T& SetIndirectLightingProvider(Args&&... args)
  {
    return Set<T>(m_indirect, std::forward<Args>(args)...);
  }
  template <class T, class... Args>
    requires std::derived_from<T, IForwardGeometryProvider>
  T& AddProvider(Args&&... args)
  {
    return Add<T>(m_geometry, std::forward<Args>(args)...);
  }
  template <class T, class... Args>
    requires std::derived_from<T, IForwardLightingProvider>
  T& SetLighting(Args&&... args)
  {
    return Set<T>(m_lighting, std::forward<Args>(args)...);
  }
  template <class T, class... Args>
    requires std::derived_from<T, IForwardShadowProvider>
  T& SetShadows(Args&&... args)
  {
    return Set<T>(m_shadows, std::forward<Args>(args)...);
  }
  template <class T, class... Args>
    requires std::derived_from<T, IForwardReflectionProvider>
  T& SetReflections(Args&&... args)
  {
    return Set<T>(m_reflections, std::forward<Args>(args)...);
  }
  template <class T, class... Args>
    requires std::derived_from<T, IForwardCompositeProvider>
  T& AddCompositor(Args&&... args)
  {
    return Add<T>(m_compositors, std::forward<Args>(args)...);
  }
  template <class T, class... Args>
    requires std::derived_from<T, IForwardPostProcess>
  T& AddForwardPostProcess(Args&&... args)
  {
    return Add<T>(m_post, std::forward<Args>(args)...);
  }
  std::uint64_t GetConfigurationRevision() const noexcept override;
  void Update(RenderUpdateContext&) override;
  RenderFeatureResult BuildGraph(RenderFeatureContext&) override;

protected:
  void ResolveDefault(RenderFeatureContext&, RenderFeatureResult&);
  virtual void BuildLightAssignment(ForwardRenderContext&) {}

private:
  std::vector<std::shared_ptr<IForwardPreparationProvider>> m_preparation;
  std::shared_ptr<ForwardRenderServices> m_rendering;
  std::shared_ptr<IForwardEnvironmentProvider> m_environment;
  std::shared_ptr<IForwardIndirectLightingProvider> m_indirect;
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
  template <class T, class... Args>
    requires std::derived_from<T, IForwardLightAssignmentProvider>
  T& SetLightAssignment(Args&&... args)
  {
    return Set<T>(m_assignment, std::forward<Args>(args)...);
  }
  std::uint64_t GetConfigurationRevision() const noexcept override;
  void Update(RenderUpdateContext&) override;

protected:
  void BuildLightAssignment(ForwardRenderContext&) override;

private:
  std::shared_ptr<IForwardLightAssignmentProvider> m_assignment;
};
} // namespace Lighthouse
