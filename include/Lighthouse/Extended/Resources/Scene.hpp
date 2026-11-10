#pragma once

#include <Lighthouse/Extended/Resources/Handle.hpp>
#include <Lighthouse/Extended/Resources/Material.hpp>
#include <Lighthouse/Extended/Resources/Model.hpp>
#include <Lighthouse/Extended/World/Scene.hpp>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace Lighthouse
{
struct GLTFScene
{
  std::filesystem::path path;
  std::shared_ptr<const GLTFAsset> asset;
};
using SceneSource = std::variant<GLTFScene>;

// Lightweight copies share a loaded CPU asset, identity, and updates.
// IDs are process-local scene IDs. Synchronize updates with reads externally.
class SceneResource
{
public:
  using ResourceID = std::uint64_t;

  // Parses the glTF immediately; referenced image files remain references as
  // defined by the existing asset loader. No renderer or GPU work is performed.
  static SceneResource FromGLTF(std::filesystem::path path)
  {
    return SceneResource(LoadGLTFSource(std::move(path)));
  }

  // Fresh entity registries, populated only. No renderer or systems are run.
  // If no default is specified, use the first scene; no scenes throws.
  Scene GetDefaultScene() const;
  Scene GetScene(std::size_t index) const;
  std::vector<Scene> GetScenes() const;
  // One model handle per glTF primitive, matching MeshRender material pairs.
  std::vector<ModelResource> GetModels() const;
  std::vector<MaterialResource> GetMaterials() const;
  std::vector<TextureResource> GetTextures() const;
  std::vector<Image> GetImages() const;

  ResourceID GetID() const noexcept
  {
    return m_state->id;
  }
  std::uint64_t GetRevision() const noexcept
  {
    return m_state->revision;
  }
  SceneSource GetSource() const
  {
    return m_state->source;
  }

  // A successful reload preserves identity and increments the revision.
  // Failure leaves all aliases unchanged; old source snapshots retain assets.
  void UpdateGLTF(std::filesystem::path path)
  {
    if (m_state->revision == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("Scene resource revision exhausted");
    auto source = LoadGLTFSource(std::move(path));
    static_assert(std::is_nothrow_move_assignable_v<SceneSource>);
    auto catalog = BuildCatalog(source);
    m_state->source = std::move(source);
    m_state->catalog = std::move(catalog);
    ++m_state->revision;
  }

  bool operator==(const SceneResource& other) const noexcept
  {
    return m_state == other.m_state;
  }

  // As with Image, moving keeps the original handle valid through a copy.
  SceneResource(const SceneResource&) noexcept = default;
  SceneResource& operator=(const SceneResource&) noexcept = default;

private:
  struct Catalog;
  static std::shared_ptr<const Catalog> BuildCatalog(const SceneSource& source);
  struct State
  {
    ResourceID id;
    std::uint64_t revision;
    SceneSource source;
    std::shared_ptr<const Catalog> catalog;
  };
  std::shared_ptr<State> m_state;

  explicit SceneResource(SceneSource source)
      : m_state(std::make_shared<State>(State{
            ResourceDetail::nextID.fetch_add(1, std::memory_order_relaxed), 1,
            source, BuildCatalog(source)}))
  {
  }

  static GLTFScene LoadGLTFSource(std::filesystem::path path)
  {
    if (path.empty())
      throw std::invalid_argument("Scene resource requires a glTF path");
    auto asset = std::make_shared<const GLTFAsset>(Assets::ParseGLTF(path));
    return {std::move(path), std::move(asset)};
  }
};
} // namespace Lighthouse
