#pragma once
#include <Lighthouse/Extended/EmbeddedResources.hpp>
#include <Lighthouse/Engine/Assets/Assets.hpp>
#include <filesystem>
namespace Lighthouse::Detail {
inline constexpr std::string_view EmbeddedPrefix = "lighthouse-extended:";
inline bool IsEmbedded(const std::filesystem::path& path) {
 return path.generic_string().starts_with(EmbeddedPrefix);
}
inline std::filesystem::path EmbeddedPath(std::filesystem::path path) {
 return std::filesystem::path(std::string(EmbeddedPrefix)) / path.lexically_normal();
}
inline std::string ResourceText(const std::filesystem::path& path) {
 const auto name = path.generic_string();
 if (IsEmbedded(path)) return ExtendedEmbedded::ReadText(std::string_view(name).substr(EmbeddedPrefix.size() + 1));
 return Assets::LoadText(path);
}
inline std::filesystem::path ResolveResource(const std::filesystem::path& path) {
 if (IsEmbedded(path)) {
   const auto normalized = path.lexically_normal();
   const auto name = normalized.generic_string();
   if (!ExtendedEmbedded::Find(std::string_view(name).substr(EmbeddedPrefix.size() + 1)))
     throw std::runtime_error("Embedded material resource not found: " + name);
   return normalized;
 }
 return std::filesystem::canonical(path);
}
}
