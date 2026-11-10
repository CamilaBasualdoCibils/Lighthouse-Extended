#pragma once
#include <Lighthouse/Engine/Assets/Assets.hpp>
#include <Lighthouse/Extended/Resources/Handle.hpp>
#include <variant>
namespace Lighthouse
{
// One glTF primitive, retaining all accessor/buffer data in its owning asset.
struct GLTFModel
{
  std::shared_ptr<const GLTFAsset> asset;
  std::uint32_t primitive;
};
struct OwnedModel
{
  std::shared_ptr<const Mesh3D> mesh;
};
using ModelSource = std::variant<GLTFModel, OwnedModel>;
class ModelResource : public ResourceDetail::Handle<ModelSource>
{
public:
  ModelResource() = default;
  static ModelResource FromGLTF(std::shared_ptr<const GLTFAsset> asset,
                                std::uint32_t primitive)
  {
    if (!asset || primitive >= asset->primitives.size())
      throw std::out_of_range("glTF primitive index");
    return ModelResource(GLTFModel{std::move(asset), primitive});
  }
  static ModelResource FromData(Mesh3D mesh)
  {
    return ModelResource(
        OwnedModel{std::make_shared<const Mesh3D>(std::move(mesh))});
  }

private:
  explicit ModelResource(ModelSource source) : Handle(std::move(source)) {}
};
using ModelHandle = ModelResource;
} // namespace Lighthouse
