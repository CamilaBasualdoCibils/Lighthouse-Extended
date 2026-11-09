#pragma once

#include <Lighthouse/Extended/Materials/Materials.hpp>
#include <array>

namespace Lighthouse::Detail
{
struct MaterialDefinition
{
  SurfaceModel model = SurfaceModel::Unlit;
  std::filesystem::path shader;
  std::string declarations;
  std::vector<MaterialParameter> parameters;
  std::vector<std::array<uint32_t, 4>> data;
  std::vector<Lighthouse::DescriptorImageInfo> textures;
};
MaterialDefinition ReadDefinition(const std::filesystem::path& path,
                                  const MaterialLoadOptions& options,
                                  bool schemaOnly = false);
} // namespace Lighthouse::Detail
