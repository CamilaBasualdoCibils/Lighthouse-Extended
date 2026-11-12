#pragma once
#include <Lighthouse/Extended/Rendering/ForwardRenderFeature.hpp>
#include <Lighthouse/Extended/Rendering/DeferredRenderFeature.hpp>
#include <Lighthouse/Extended/Rendering/PathTracingRenderFeature.hpp>
#include <Lighthouse/Extended/Rendering/DrawCollector.hpp>
#include <Lighthouse/Extended/World/Entity/MeshRender.hpp>
#include <Lighthouse/Extended/World/Scene.hpp>
#include <array>
#include <unordered_map>

namespace Lighthouse
{
// Shared scene extraction and per-device mesh/image/material allocation cache.
// Scene and render devices must outlive this store. Providers own their specialized
// pipelines, batches and view-dependent bindings.
class SceneRenderStore
{
public:
  explicit SceneRenderStore(Scene& scene);
  ~SceneRenderStore();
  void Update(RenderUpdateContext&);
  std::uint64_t GetRevision() const noexcept { return m_revision; }
  Scene& GetScene() const noexcept { return *m_world; }
  Mat4 GetWorldMatrix(entt::entity entity) const;
  struct Instance { entt::entity entity; ModelHandle mesh; MaterialHandle material; Mat4 transform; };
  std::vector<Instance> GetInstances() const;
  std::shared_ptr<struct SceneMeshGPU> GetMesh(IRenderDevice&, const ModelHandle&);
  std::shared_ptr<struct SceneMaterialGPU> GetMaterial(IRenderDevice&, const MaterialHandle&);
  struct GPUCache;

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
  std::uint64_t m_revision{};
  std::unique_ptr<GPUCache> m_gpu;
  Scene* m_world;
  DrawCollector m_draws;
  std::unordered_map<entt::entity, SceneDraw> m_sceneDraws;
  std::vector<DrawInstanceHandle> m_removedDraws;
  const ResourceRequests* m_lastResources{};
  std::uint64_t m_lastFrame{};
  bool m_updated{};
};
using SceneRenderData = SceneRenderStore;

class SceneDeferredRenderProvider : public IDeferredGeometryProvider
{
public:
  explicit SceneDeferredRenderProvider(std::shared_ptr<SceneRenderStore> scene);
  void Update(RenderUpdateContext& ctx) override { m_scene->Update(ctx); }
  std::uint64_t GetConfigurationRevision() const noexcept override { return m_scene->GetRevision(); }
  void BuildGraph(DeferredRenderContext&) override;
  const SceneRenderStore& GetSceneData() const { return *m_scene; }
private:
  struct History;
  std::shared_ptr<History> m_history;
  std::shared_ptr<SceneRenderStore> m_scene;
};
class SceneForwardRenderProvider : public IForwardGeometryProvider
{
public:
  explicit SceneForwardRenderProvider(std::shared_ptr<SceneRenderStore> scene) : m_scene(std::move(scene))
  { if (!m_scene) throw std::invalid_argument("Null scene render data"); }
  void Update(RenderUpdateContext& ctx) override { m_scene->Update(ctx); }
  std::uint64_t GetConfigurationRevision() const noexcept override { return m_scene->GetRevision(); }
  void BuildGraph(ForwardRenderContext&) override; // Also usable by ForwardPlusRenderFeature.
private:
  std::shared_ptr<SceneRenderStore> m_scene;
};
class SceneRaytracedRenderProvider : public IRaySceneProvider
{
public:
  explicit SceneRaytracedRenderProvider(std::shared_ptr<SceneRenderStore> scene) : m_scene(std::move(scene))
  { if (!m_scene) throw std::invalid_argument("Null scene render data"); }
  void Update(RenderUpdateContext& ctx) override { m_scene->Update(ctx); }
  void BuildGraph(PathTracingRenderContext&) override;
private:
  std::shared_ptr<SceneRenderStore> m_scene;
};
} // namespace Lighthouse

namespace Lighthouse { using SceneProvider = SceneForwardRenderProvider; }
