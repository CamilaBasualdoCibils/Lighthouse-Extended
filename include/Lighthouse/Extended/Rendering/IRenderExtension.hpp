#pragma once
#include <Lighthouse/Extended/World/CameraView.hpp>
#include <Lighthouse/Extended/World/Scene.hpp>
namespace Lighthouse
{
enum class RenderStage
{
  BeforeGeometry,
  Geometry,
  AfterGeometry,
  BeforeLighting,
  AfterLighting,
  BeforePresentation
};
struct RenderContext
{
  TechniqueBuildContext& technique;
  Scene& scene;
  entt::entity cameraEntity;
  CameraView& camera;
  GBufferResources& gbuffer;
  RenderGraphResource& color;
  Mat4 viewProjection;
  float deltaTime;
  std::uint64_t buildId;
};
class IRenderExtension
{
public:
  virtual ~IRenderExtension() = default;
  virtual void BuildGraph(RenderGraph& graph, RenderStage stage,
                          RenderContext& context) = 0;
};
} // namespace Lighthouse
