#pragma once
#include <Lighthouse/Extended/Rendering/ResourceRequests.hpp>
#include <Lighthouse/Extended/Math/Types.hpp>
#include <atomic>
#include <unordered_map>
#include <stdexcept>

namespace Lighthouse
{
struct DrawInstanceTag;
using DrawInstanceHandle = Handle<DrawInstanceTag>;

struct DrawInstance
{
  RenderObjectHandle mesh{}, material{};
  Mat4 worldTransform{1.0f};
  bool visible{true};
  std::uint64_t revision{1};
};

// Persistent renderer-owned instances, shared by all extensions.
class DrawCollector
{
public:
  DrawCollector() : m_owner(s_nextOwner.fetch_add(1)) {}
  DrawCollector(const DrawCollector&) = delete;
  DrawCollector& operator=(const DrawCollector&) = delete;

  DrawInstanceHandle Add(DrawInstance instance)
  {
    if (m_nextIndex == DrawInstanceHandle::InvalidIndex)
      throw std::overflow_error("Draw instance handles exhausted");
    const DrawInstanceHandle handle{m_nextIndex++, 1, m_owner};
    instance.revision = 1;
    m_instances.emplace(handle, std::move(instance));
    return handle;
  }
  void Update(DrawInstanceHandle handle, DrawInstance instance)
  {
    auto& current = m_instances.at(handle);
    instance.revision = current.revision + 1;
    current = std::move(instance);
  }
  void Remove(DrawInstanceHandle handle) { m_instances.erase(handle); }
  const auto& GetInstances() const noexcept { return m_instances; }

private:
  inline static std::atomic<std::uint64_t> s_nextOwner{1};
  std::uint64_t m_owner;
  std::uint32_t m_nextIndex{};
  std::unordered_map<DrawInstanceHandle, DrawInstance> m_instances;
};
} // namespace Lighthouse
