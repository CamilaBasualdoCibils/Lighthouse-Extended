#pragma once
#include <Lighthouse/Extended/Resources/Texture.hpp>
#include <Lighthouse/Extended/Materials/Materials.hpp>
#include <map>
#include <string>
namespace Lighthouse
{
struct GLTFMaterial
{
  std::shared_ptr<const GLTFAsset> asset;
  std::uint32_t material;
};
struct OwnedMaterial
{
  MaterialAsset data;
};
struct DefinedMaterial
{
  std::filesystem::path path;
  std::map<std::string, MaterialValue> parameters;
};
using MaterialSource = std::variant<GLTFMaterial, OwnedMaterial, DefinedMaterial>;
class MaterialResource : public ResourceDetail::Handle<MaterialSource>
{
public:
  MaterialResource() = default;
  MaterialResource* operator->()
  {
    Check();
    return this;
  }
  const MaterialResource* operator->() const
  {
    Check();
    return this;
  }
  static MaterialResource FromGLTF(std::shared_ptr<const GLTFAsset> asset,
                                   std::uint32_t index)
  {
    if (!asset || index >= asset->materials.size())
      throw std::out_of_range("glTF material index");
    return MaterialResource(GLTFMaterial{std::move(asset), index});
  }
  static MaterialResource FromData(MaterialAsset data = {})
  {
    return MaterialResource(OwnedMaterial{std::move(data)});
  }
  static MaterialResource FromDefinition(std::filesystem::path path,
      std::map<std::string, MaterialValue> parameters = {})
  {
    if (path.empty()) throw std::invalid_argument("Material definition requires a path");
    return MaterialResource(DefinedMaterial{std::move(path), std::move(parameters)});
  }
  void SetTexture(std::string name, TextureResource texture)
  {
    Check();
    if (!texture)
      throw std::invalid_argument("Material requires a texture handle");
    auto updated = *textures;
    updated.insert_or_assign(std::move(name), std::move(texture));
    Touch();
    textures->swap(updated);
  }
  const std::map<std::string, TextureResource>& GetTextures() const
  {
    Check();
    return *textures;
  }
  TextureResource GetTexture(const std::string& name) const
  {
    return GetTextures().at(name);
  }

private:
  explicit MaterialResource(MaterialSource source)
      : Handle(std::move(source)),
        textures(std::make_shared<std::map<std::string, TextureResource>>())
  {
  }
  std::shared_ptr<std::map<std::string, TextureResource>> textures;
};
using MaterialHandle = MaterialResource;
} // namespace Lighthouse
