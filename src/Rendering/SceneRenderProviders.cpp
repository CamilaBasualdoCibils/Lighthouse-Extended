#include <Lighthouse/Extended/Rendering/SceneRenderProviders.hpp>
#include <Lighthouse/Extended/World/Entity/Transform.hpp>
#include <unordered_set>
#include <functional>
namespace Lighthouse
{
SceneRenderData::SceneRenderData(Scene& scene) : m_world(&scene) {}
void SceneRenderData::Update(RenderUpdateContext& context)
{
  if (m_updated && m_lastResources == &context.resources && m_lastFrame == context.frameId) return;
  CollectResources(context.resources);
  CollectDraws(m_draws);
  m_lastResources = &context.resources;
  m_lastFrame = context.frameId;
  m_updated = true;
}
void SceneRenderData::CollectResources(ResourceRequests& requests)
{
  auto& registry = m_world->Registry();
  std::unordered_set<entt::entity> present, visiting;
  std::unordered_map<entt::entity, Mat4> matrices;
  std::function<Mat4(entt::entity)> worldMatrix = [&](entt::entity entity) -> Mat4 {
    if (const auto it = matrices.find(entity); it != matrices.end()) return it->second;
    if (!visiting.insert(entity).second)
      throw std::logic_error("Cycle in scene transform hierarchy");
    const auto& transform = registry.get<Transform>(entity);
    auto matrix = transform.GetMatrix();
    if (const auto parent = transform.GetParent(); parent && registry.valid(*parent) &&
        registry.all_of<Transform>(*parent))
      matrix = worldMatrix(*parent) * matrix;
    visiting.erase(entity);
    matrices.emplace(entity, matrix);
    return matrix;
  };
  for (const auto entity : registry.view<MeshRender, Transform>())
  {
    present.insert(entity);
    const auto& mesh = registry.get<MeshRender>(entity);
    const auto matrix = worldMatrix(entity);
    auto [it, inserted] = m_sceneDraws.try_emplace(entity);
    auto& cached = it->second;
    if (inserted || cached.mesh != mesh.mesh || cached.meshRevision != mesh.mesh.GetRevision())
    {
      cached.mesh = mesh.mesh;
      cached.meshRevision = mesh.mesh.GetRevision();
      cached.renderMesh = mesh.mesh ? requests.LoadMesh(mesh.mesh) : RenderObjectHandle{};
      cached.dirty = true;
    }
    std::vector<std::array<std::uint64_t, 3>> textures;
    if (mesh.material)
      for (const auto& [_, texture] : mesh.material.GetTextures())
        textures.push_back({texture.GetID(), texture.GetRevision(), texture.GetImage().GetRevision()});
    if (inserted || cached.material != mesh.material ||
        cached.materialRevision != mesh.material.GetRevision() || cached.textures != textures)
    {
      cached.material = mesh.material;
      cached.materialRevision = mesh.material.GetRevision();
      cached.textures = std::move(textures);
      cached.renderMaterial = mesh.material ? requests.LoadMaterial(mesh.material) : RenderObjectHandle{};
      if (mesh.material)
        for (const auto& [_, texture] : mesh.material.GetTextures()) requests.LoadTexture(texture);
      cached.dirty = true;
    }
    if (inserted || cached.transform != matrix)
    {
      cached.transform = matrix;
      cached.dirty = true;
    }
  }
  for (auto it = m_sceneDraws.begin(); it != m_sceneDraws.end();)
  {
    if (present.contains(it->first)) { ++it; continue; }
    if (it->second.instance) m_removedDraws.push_back(it->second.instance);
    it = m_sceneDraws.erase(it);
  }
}

void SceneRenderData::CollectDraws(DrawCollector& draws)
{
  for (const auto handle : m_removedDraws) draws.Remove(handle);
  m_removedDraws.clear();
  for (auto& [_, cached] : m_sceneDraws)
  {
    if (!cached.dirty) continue;
    DrawInstance instance{cached.renderMesh, cached.renderMaterial, cached.transform,
                          bool(cached.mesh && cached.material)};
    if (cached.instance) draws.Update(cached.instance, instance);
    else cached.instance = draws.Add(instance);
    cached.dirty = false;
  }
}


void SceneRaytracedRenderProvider::BuildGraph(PathTracingRenderContext& context)
{
  for (const auto& [_, draw] : m_scene->GetDraws().GetInstances())
    if (draw.visible)
      context.scene.instances.push_back({draw.mesh, draw.material, draw.worldTransform});
}
} // namespace Lighthouse
