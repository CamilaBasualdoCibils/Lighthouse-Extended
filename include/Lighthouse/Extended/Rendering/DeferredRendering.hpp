#pragma once
#include <Lighthouse/Extended/Rendering/DeferredRenderFeature.hpp>
#include <Lighthouse/Extended/Rendering/ForwardRendering.hpp>
namespace Lighthouse
{
struct alignas(16) DeferredFrameData
{
  Mat4 viewProjection{1}, model{1}, previousViewProjection{1}, previousModel{1};
};
// The deferred feature owns attachment planning and material-compatible
// pipelines.
class DeferredRenderServices
{
public:
  DeferredRenderServices(IRenderDevice&, DeferredDependencies);
  ~DeferredRenderServices();
  void BeginGeometry(DeferredRenderContext&);
  GraphicsPipelineHandle GetPipeline(const VertexLayout&,
                                     std::shared_ptr<SceneMaterialGPU>);
  std::shared_ptr<const MaterialInstance>
      GetMaterial(std::shared_ptr<SceneMaterialGPU>);
  const std::vector<GBufferAttribute>& Attributes() const;
  void Shade(DeferredRenderContext&, std::string lightingShader = {},
             std::function<std::vector<LightData>()> lights = {},
             Vec3 ambient = Vec3(0));

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
class DefaultDeferredLightingProvider final : public IDeferredLightingProvider
{
public:
  explicit DefaultDeferredLightingProvider(std::shared_ptr<SceneRenderStore>,
                                           Vec3 ambientRadiance = Vec3(0));
  void Update(RenderUpdateContext&) override;
  DeferredDependencies GetDeferredDependencies() const override;
  void BuildGraph(DeferredRenderContext&) override;

private:
  DefaultForwardLightingProvider m_sceneLights;
  Vec3 m_ambient;
};
} // namespace Lighthouse
