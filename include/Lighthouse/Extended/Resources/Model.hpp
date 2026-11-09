#pragma once

#include <Lighthouse/Engine/Assets/Assets.hpp>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace Lighthouse
{
struct GLTFModel
{
  std::filesystem::path path;
  std::shared_ptr<const GLTFAsset> asset;
};
using ModelSource = std::variant<GLTFModel>;

// Lightweight copies share a loaded CPU asset, identity, and updates.
// IDs are process-local model IDs. Synchronize updates with reads externally.
class Model
{
public:
  using ResourceID = std::uint64_t;

  // Parses the glTF immediately; referenced image files remain references as
  // defined by the existing asset loader. No renderer or GPU work is performed.
  static Model FromGLTF(std::filesystem::path path)
  {
    return Model(LoadGLTFSource(std::move(path)));
  }

  ResourceID GetID() const noexcept { return m_state->id; }
  std::uint64_t GetRevision() const noexcept { return m_state->revision; }
  ModelSource GetSource() const { return m_state->source; }

  // A successful reload preserves identity and increments the revision.
  // Failure leaves all aliases unchanged; old source snapshots retain assets.
  void UpdateGLTF(std::filesystem::path path)
  {
    if (m_state->revision == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("Model resource revision exhausted");
    auto source = LoadGLTFSource(std::move(path));
    static_assert(std::is_nothrow_move_assignable_v<ModelSource>);
    m_state->source = std::move(source);
    ++m_state->revision;
  }

  bool operator==(const Model& other) const noexcept
  {
    return m_state == other.m_state;
  }

  // As with Image, moving keeps the original handle valid through a copy.
  Model(const Model&) noexcept = default;
  Model& operator=(const Model&) noexcept = default;

private:
  struct State
  {
    ResourceID id;
    std::uint64_t revision;
    ModelSource source;
  };
  inline static std::atomic<ResourceID> s_nextID{1};
  std::shared_ptr<State> m_state;

  explicit Model(ModelSource source)
      : m_state(std::make_shared<State>(State{
            s_nextID.fetch_add(1, std::memory_order_relaxed), 1,
            std::move(source)}))
  {
  }

  static GLTFModel LoadGLTFSource(std::filesystem::path path)
  {
    if (path.empty())
      throw std::invalid_argument("Model resource requires a glTF path");
    auto asset = std::make_shared<const GLTFAsset>(Assets::ParseGLTF(path));
    return {std::move(path), std::move(asset)};
  }
};
} // namespace Lighthouse
