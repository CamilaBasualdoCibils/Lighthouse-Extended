#pragma once
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
namespace Lighthouse::ResourceDetail
{
inline std::atomic<std::uint64_t> nextID{1};
template <class Source> class Handle
{
public:
  Handle() = default;
  explicit operator bool() const noexcept
  {
    return bool(state);
  }
  std::uint64_t GetID() const noexcept
  {
    return state ? state->id : 0;
  }
  std::uint64_t GetRevision() const noexcept
  {
    return state ? state->revision : 0;
  }
  Source GetSource() const
  {
    Check();
    return state->source;
  }
  bool operator==(const Handle& other) const noexcept
  {
    return state == other.state;
  }

protected:
  explicit Handle(Source source)
      : state(std::make_shared<State>(
            State{nextID.fetch_add(1, std::memory_order_relaxed), 1,
                  std::move(source)}))
  {
  }
  void Check() const
  {
    if (!state)
      throw std::logic_error("Empty resource handle");
  }
  void Touch()
  {
    Check();
    if (state->revision == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("Resource revision exhausted");
    ++state->revision;
  }
  struct State
  {
    std::uint64_t id, revision;
    Source source;
  };
  std::shared_ptr<State> state;
};
} // namespace Lighthouse::ResourceDetail
