#pragma once

#include <Lighthouse/Extended/Resources/Image.hpp>
#include <Lighthouse/Extended/Resources/Model.hpp>

namespace Lighthouse
{
class Resource
{
public:
  using Image = Lighthouse::Image;
  using ResourceID = Image::ResourceID;
  using ImageLoadOptions = Lighthouse::ImageLoadOptions;
  using FileImage = Lighthouse::FileImage;
  using MemoryImage = Lighthouse::MemoryImage;
  using GeneratedImage = Lighthouse::GeneratedImage;
  using ImageSource = Lighthouse::ImageSource;

  using Model = Lighthouse::Model;
  using GLTFModel = Lighthouse::GLTFModel;
  using ModelSource = Lighthouse::ModelSource;

  static Model LoadModel(std::filesystem::path path)
  {
    return Model::FromGLTF(std::move(path));
  }

  static Image LoadImage(std::filesystem::path path,
                         ImageLoadOptions options = {})
  {
    return Image::FromFile(std::move(path), options);
  }
};
} // namespace Lighthouse
