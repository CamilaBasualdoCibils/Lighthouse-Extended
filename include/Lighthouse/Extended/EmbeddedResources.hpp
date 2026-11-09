#pragma once
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
namespace Lighthouse::ExtendedEmbedded {
// Paths are relative to this repository's res/ directory. Binary data is
// immutable and remains valid for the executable's lifetime, including NULs.
std::optional<std::span<const std::byte>> Find(std::string_view path) noexcept;
std::string ReadText(std::string_view path);
}
