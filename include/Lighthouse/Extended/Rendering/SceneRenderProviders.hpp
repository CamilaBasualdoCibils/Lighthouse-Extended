#pragma once
#include <Lighthouse/Extended/Rendering/RenderFeatures.hpp>
#include <Lighthouse/Extended/Rendering/DrawCollector.hpp>
#include <Lighthouse/Extended/World/Entity/MeshRender.hpp>
#include <Lighthouse/Extended/World/Scene.hpp>
#include <array>
#include <unordered_map>

namespace Lighthouse
{
// Shared extraction state for adapters of the same scene. No GPU resource loader.
// Scene must outlive this state. Each provider can retain its own specialized batches.
class SceneRenderData
{
public:
  explicit SceneRenderData(Scene& scene);
  void Update(RenderUpdateContext&);
  const DrawCollector& GetDraws() const noexcept { return m_draws; }
private:
  void CollectResources(ResourceRequests&);
  void CollectDraws(DrawCollector&);
  struct SceneDraw
  {
    ModelHandle mesh;
    MaterialHandle material;
    std::uint64_t meshRevision{}, materialRevision{};
    std::vector<std::array<std::uint64_t, 3>> textures;
    Mat4 transform{1.0f};
    RenderObjectHandle renderMesh{}, renderMaterial{};
    DrawInstanceHandle instance{};
    bool dirty{true};
  };
  Scene* m_world;
  DrawCollector m_draws;
  std::unordered_map<entt::entity, SceneDraw> m_sceneDraws;
  std::vector<DrawInstanceHandle> m_removedDraws;
  const ResourceRequests* m_lastResources{};
  std::uint64_t m_lastFrame{};
  bool m_updated{};
};
class SceneDeferredRenderProvider : public IDeferredGeometryProvider
{
public:
  explicit SceneDeferredRenderProvider(std::shared_ptr<SceneRenderData> scene) : m_scene(std::move(scene))
  { if (!m_scene) throw std::invalid_argument("Null scene render data"); }
  void Update(RenderUpdateContext& ctx) override { m_scene->Update(ctx); }
  void BuildGraph(DeferredRenderContext&) override {} // Raster rendering intentionally deferred.
  const SceneRenderData& GetSceneData() const { return *m_scene; }
private:
  std::shared_ptr<SceneRenderData> m_scene;
};
class SceneForwardRenderProvider : public IForwardGeometryProvider
{
public:
  explicit SceneForwardRenderProvider(std::shared_ptr<SceneRenderData> scene) : m_scene(std::move(scene))
  { if (!m_scene) throw std::invalid_argument("Null scene render data"); }
  void Update(RenderUpdateContext& ctx) override { m_scene->Update(ctx); }
  void BuildGraph(ForwardRenderContext&) override {} // Also usable by ForwardPlusRenderFeature.
private:
  std::shared_ptr<SceneRenderData> m_scene;
};
class SceneRaytracedRenderProvider : public IRaySceneProvider
{
public:
  explicit SceneRaytracedRenderProvider(std::shared_ptr<SceneRenderData> scene) : m_scene(std::move(scene))
  { if (!m_scene) throw std::invalid_argument("Null scene render data"); }
  void Update(RenderUpdateContext& ctx) override { m_scene->Update(ctx); }
  void BuildGraph(PathTracingRenderContext&) override;
private:
  std::shared_ptr<SceneRenderData> m_scene;
};
} // namespace Lighthouse
