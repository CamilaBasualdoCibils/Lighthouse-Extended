#include <Lighthouse/Extended/Resources/Scene.hpp>
#include <Lighthouse/Extended/World/Entity/Camera.hpp>
#include <Lighthouse/Extended/World/Entity/MeshRender.hpp>
#include <Lighthouse/Extended/World/Entity/Transform.hpp>
#include <optional>

namespace Lighthouse
{
TextureResource
TextureResource::FromGLTF(std::shared_ptr<const GLTFAsset> asset,
                          std::uint32_t index, Image image)
{
  if (!asset || index >= asset->textures.size())
    throw std::out_of_range("glTF texture index");
  const auto& texture = asset->textures[index];
  SamplerDesc sampler;
  if (texture.sampler)
  {
    const auto& source = asset->samplers.at(*texture.sampler);
    auto address = [](std::uint16_t mode)
    {
      switch (mode)
      {
      case 10497:
        return SamplerAddressMode::Repeat;
      case 33648:
        return SamplerAddressMode::MirroredRepeat;
      case 33071:
        return SamplerAddressMode::ClampToEdge;
      default:
        throw std::invalid_argument("Invalid glTF sampler wrap mode");
      }
    };
    sampler.addressU = address(source.wrapS);
    sampler.addressV = address(source.wrapT);
    switch (source.magFilter.value_or(9729))
    {
    case 9728:
      sampler.magnification = SamplerFilter::Nearest;
      break;
    case 9729:
      sampler.magnification = SamplerFilter::Linear;
      break;
    default:
      throw std::invalid_argument("Invalid glTF magnification filter");
    }
    switch (source.minFilter.value_or(9729))
    {
    case 9728:
      sampler.minification = SamplerFilter::Nearest;
      break;
    case 9729:
      break;
    case 9984:
      sampler.minification = SamplerFilter::Nearest;
      sampler.mipmap = SamplerMipmapFilter::Nearest;
      break;
    case 9985:
      sampler.mipmap = SamplerMipmapFilter::Nearest;
      break;
    case 9986:
      sampler.minification = SamplerFilter::Nearest;
      sampler.mipmap = SamplerMipmapFilter::Linear;
      break;
    case 9987:
      sampler.mipmap = SamplerMipmapFilter::Linear;
      break;
    default:
      throw std::invalid_argument("Invalid glTF minification filter");
    }
  }
  const auto imageSource = image.GetSource();
  const auto* gltfImage = std::get_if<GLTFImage>(&imageSource);
  if (!gltfImage || gltfImage->asset != asset ||
      gltfImage->image != texture.image)
    throw std::invalid_argument("Texture image does not match glTF source");
  return TextureResource(
      GLTFTexture{std::move(asset), index, std::move(image), sampler});
}

struct SceneResource::Catalog
{
  std::vector<ModelResource> models;
  std::vector<MaterialResource> materials;
  std::vector<TextureResource> textures;
  std::vector<Image> images;
  MaterialResource defaultMaterial = MaterialResource::FromData();
};

std::shared_ptr<const SceneResource::Catalog>
SceneResource::BuildCatalog(const SceneSource& source)
{
  const auto asset = std::get<GLTFScene>(source).asset;
  auto catalog = std::make_shared<Catalog>();
  for (std::uint32_t i = 0; i < asset->images.size(); ++i)
    catalog->images.push_back(Image::FromGLTF(asset, i));
  for (std::uint32_t i = 0; i < asset->textures.size(); ++i)
    catalog->textures.push_back(TextureResource::FromGLTF(
        asset, i, catalog->images.at(asset->textures[i].image)));
  for (std::uint32_t i = 0; i < asset->materials.size(); ++i)
  {
    auto material = MaterialResource::FromGLTF(asset, i);
    const auto& data = asset->materials[i];
    auto bind = [&](const char* name, const auto& info)
    {
      if (info)
        material.SetTexture(name, catalog->textures.at(info->texture));
    };
    bind("baseColor", data.baseColorTexture);
    bind("metallicRoughness", data.metallicRoughnessTexture);
    bind("normal", data.normalTexture);
    bind("occlusion", data.occlusionTexture);
    bind("emissive", data.emissiveTexture);
    catalog->materials.push_back(std::move(material));
  }
  for (std::uint32_t i = 0; i < asset->primitives.size(); ++i)
  {
    const auto material = asset->primitives[i].material;
    if (material && *material >= catalog->materials.size())
      throw std::out_of_range("glTF material index");
    catalog->models.push_back(ModelResource::FromGLTF(asset, i));
  }
  return catalog;
}

Scene SceneResource::GetDefaultScene() const
{
  const auto asset = std::get<GLTFScene>(m_state->source).asset;
  if (asset->scenes.empty())
    throw std::out_of_range("glTF contains no scenes");
  // glTF may omit its default scene. In that case choose the first scene.
  return GetScene(asset->defaultScene.value_or(0));
}

Scene SceneResource::GetScene(std::size_t index) const
{
  const auto asset = std::get<GLTFScene>(m_state->source).asset;
  const auto catalog = m_state->catalog;
  const auto& description = asset->scenes.at(index);
  Scene scene(description.name);
  struct Pending
  {
    std::uint32_t node;
    std::optional<EntityID> parent;
  };
  std::vector<Pending> pending;
  for (auto it = description.rootNodes.rbegin();
       it != description.rootNodes.rend(); ++it)
    pending.push_back({*it, {}});
  std::vector<bool> visited(asset->nodes.size());
  while (!pending.empty())
  {
    const auto item = pending.back();
    pending.pop_back();
    const auto& node = asset->nodes.at(item.node);
    if (visited.at(item.node))
      throw std::invalid_argument(
          "glTF scene contains a cycle or repeated node");
    visited[item.node] = true;
    auto entity = scene.CreateEntity(node.name);
    auto& transform = entity.Add<Transform>();
    transform.SetMatrix(node.localTransform);
    if (item.parent)
      transform.SetParent(*item.parent);
    if (node.camera)
      entity.Add<Camera>(asset->cameras.at(*node.camera));
    if (node.skin)
      throw std::invalid_argument(
          "Skinned glTF scenes are not supported by the asset loader");
    if (node.mesh)
    {
      const auto& mesh = asset->meshes.at(*node.mesh);
      for (std::size_t part = 0; part < mesh.primitives.size(); ++part)
      {
        const auto primitive = mesh.primitives[part];
        const auto& data = asset->primitives.at(primitive);
        auto material = data.material ? catalog->materials.at(*data.material)
                                      : catalog->defaultMaterial;
        // MeshRender describes one primitive/material pair. Additional
        // primitives get identity-transform children under the glTF node.
        if (part == 0)
          entity.Add<MeshRender>(catalog->models.at(primitive), material);
        else
        {
          auto child = scene.CreateEntity(node.name + " primitive " +
                                          std::to_string(part));
          child.Add<Transform>().SetParent(entity.ID());
          child.Add<MeshRender>(catalog->models.at(primitive), material);
        }
      }
    }
    for (auto it = node.children.rbegin(); it != node.children.rend(); ++it)
      pending.push_back({*it, entity.ID()});
  }
  return scene;
}

std::vector<Scene> SceneResource::GetScenes() const
{
  const auto asset = std::get<GLTFScene>(m_state->source).asset;
  std::vector<Scene> scenes;
  scenes.reserve(asset->scenes.size());
  for (std::size_t i = 0; i < asset->scenes.size(); ++i)
    scenes.push_back(GetScene(i));
  return scenes;
}
std::vector<ModelResource> SceneResource::GetModels() const
{
  return m_state->catalog->models;
}
std::vector<MaterialResource> SceneResource::GetMaterials() const
{
  return m_state->catalog->materials;
}
std::vector<TextureResource> SceneResource::GetTextures() const
{
  return m_state->catalog->textures;
}
std::vector<Image> SceneResource::GetImages() const
{
  return m_state->catalog->images;
}
} // namespace Lighthouse
