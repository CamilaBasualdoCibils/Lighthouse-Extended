#pragma once

#include <Lighthouse/Engine/Assets/ImageData.hpp>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace Lighthouse
{
// Instructions for a future decoder; creating a resource never opens a file.
struct ImageLoadOptions
{
  std::optional<ImageFormat> format;
  bool flipY = false;
};

struct FileImage
{
  std::filesystem::path path;
  ImageLoadOptions options;
};
struct MemoryImage
{
  std::shared_ptr<const ImageData> data;
};
struct GeneratedImage
{
  std::function<ImageData()> generator;
};
using ImageSource = std::variant<FileImage, MemoryImage, GeneratedImage>;

// Copies share identity and updates. Separate factory calls create distinct
// resources, even for the same file. IDs are process-local, not serialized keys.
// Concurrent reads are supported; callers must synchronize updates with reads.
class Image
{
public:
  using ResourceID = std::uint64_t;
  static Image FromFile(std::filesystem::path path,
                        ImageLoadOptions options)
  {
    if (path.empty())
      throw std::invalid_argument("Image resource requires a file path");
    return Image(FileImage{std::move(path), options});
  }

  static Image FromFile(std::filesystem::path path)
  {
    return FromFile(std::move(path), ImageLoadOptions{});
  }

  // ImageData owns its pixels: lvalues are copied and rvalues transfer ownership.
  static Image FromData(ImageData data)
  {
    return Image(MemoryImage{std::make_shared<const ImageData>(std::move(data))});
  }

  // The callback is stored, never invoked here. Captured dependencies must live
  // as long as the callback; a future resolver decides when to execute it.
  static Image FromGenerator(std::function<ImageData()> generator)
  {
    if (!generator)
      throw std::invalid_argument("Image resource requires a generator");
    return Image(GeneratedImage{std::move(generator)});
  }

  ResourceID GetID() const noexcept { return m_state->id; }
  std::uint64_t GetRevision() const noexcept { return m_state->revision; }

  // A snapshot retains owned pixels/callbacks across subsequent updates.
  ImageSource GetSource() const { return m_state->source; }

  void UpdateData(ImageData data)
  {
    Replace(MemoryImage{std::make_shared<const ImageData>(std::move(data))});
  }
  void UpdateFile(std::filesystem::path path, ImageLoadOptions options)
  {
    if (path.empty())
      throw std::invalid_argument("Image resource requires a file path");
    Replace(FileImage{std::move(path), options});
  }
  void UpdateFile(std::filesystem::path path)
  {
    UpdateFile(std::move(path), ImageLoadOptions{});
  }
  void UpdateGenerator(std::function<ImageData()> generator)
  {
    if (!generator)
      throw std::invalid_argument("Image resource requires a generator");
    Replace(GeneratedImage{std::move(generator)});
  }

  // Explicitly signal external file changes or changed generator inputs.
  // No filesystem polling, generation, decoding, or GPU work is performed.
  void MarkDirty()
  {
    CheckRevision();
    ++m_state->revision;
  }

  bool operator==(const Image& other) const noexcept
  {
    return m_state == other.m_state;
  }

  // Moving behaves like copying so every handle remains valid and usable.
  Image(const Image&) noexcept = default;
  Image& operator=(const Image&) noexcept = default;

private:
  struct State
  {
    ResourceID id;
    std::uint64_t revision = 1;
    ImageSource source;
  };
  inline static std::atomic<ResourceID> s_nextID{1};
  std::shared_ptr<State> m_state;

  explicit Image(ImageSource source)
      : m_state(std::make_shared<State>(State{
            s_nextID.fetch_add(1, std::memory_order_relaxed), 1,
            std::move(source)}))
  {
  }
  void CheckRevision() const
  {
    if (m_state->revision == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("Image resource revision exhausted");
  }
  void Replace(ImageSource source)
  {
    CheckRevision();
    static_assert(std::is_nothrow_move_assignable_v<ImageSource>);
    m_state->source = std::move(source);
    ++m_state->revision;
  }
};

} // namespace Lighthouse
