#pragma once
#include <Lighthouse/Extended/Resources/Scene.hpp>
namespace Lighthouse
{
class Resource
{
public:
  using Image = Lighthouse::Image;
  using ResourceID = Image::ResourceID;
  using ImageSource = Lighthouse::ImageSource;
  using ImageLoadOptions = Lighthouse::ImageLoadOptions;
  using MipPolicy = Lighthouse::MipPolicy;
  using FileImage = Lighthouse::FileImage;
  using MemoryImage = Lighthouse::MemoryImage;
  using GeneratedImage = Lighthouse::GeneratedImage;
  using GLTFImage = Lighthouse::GLTFImage;
  using Scene = Lighthouse::SceneResource;
  using SceneSource = Lighthouse::SceneSource;
  using GLTFScene = Lighthouse::GLTFScene;
  using Model = Lighthouse::ModelResource;
  using ModelSource = Lighthouse::ModelSource;
  using GLTFModel = Lighthouse::GLTFModel;
  using Material = Lighthouse::MaterialResource;
  using MaterialSource = Lighthouse::MaterialSource;
  using GLTFMaterial = Lighthouse::GLTFMaterial;
  using Texture = Lighthouse::TextureResource;
  using ModelHandle = Lighthouse::ModelHandle;
  using MaterialHandle = Lighthouse::MaterialHandle;
  using TextureHandle = Lighthouse::TextureHandle;
  using TextureSource = Lighthouse::TextureSource;
  static Scene LoadScene(std::filesystem::path path)
  {
    return Scene::FromGLTF(std::move(path));
  }
  static Image LoadImage(std::filesystem::path path,
                         ImageLoadOptions options = {})
  {
    return Image::FromFile(std::move(path), options);
  }
  static Texture LoadTexture(std::filesystem::path path,
                             SamplerDesc sampler = {},
                             ImageLoadOptions options = {})
  {
    return Texture::FromImage(LoadImage(std::move(path), options), sampler);
  }
  static Texture LoadTexture(Image image, SamplerDesc sampler = {})
  {
    return Texture::FromImage(std::move(image), sampler);
  }
};
} // namespace Lighthouse
