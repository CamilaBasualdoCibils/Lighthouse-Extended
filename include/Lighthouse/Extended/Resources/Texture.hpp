#pragma once
#include <Lighthouse/Extended/Resources/Handle.hpp>
#include <Lighthouse/Extended/Resources/Image.hpp>
namespace Lighthouse
{
struct ImageTexture
{
  Image image;
  SamplerDesc sampler;
};
struct GLTFTexture
{
  std::shared_ptr<const GLTFAsset> asset;
  std::uint32_t texture;
  Image image;
  SamplerDesc sampler;
};
using TextureSource = std::variant<ImageTexture, GLTFTexture>;
class TextureResource : public ResourceDetail::Handle<TextureSource>
{
public:
  TextureResource() = default;
  static TextureResource FromImage(Image image, SamplerDesc sampler = {})
  {
    return TextureResource(ImageTexture{std::move(image), sampler});
  }
  static TextureResource FromGLTF(std::shared_ptr<const GLTFAsset> asset,
                                  std::uint32_t index, Image image);
  Image GetImage() const
  {
    return std::visit([](const auto& s) { return s.image; }, GetSource());
  }
  SamplerDesc GetSampler() const
  {
    return std::visit([](const auto& s) { return s.sampler; }, GetSource());
  }

private:
  explicit TextureResource(TextureSource source) : Handle(std::move(source)) {}
};
using TextureHandle = TextureResource;
} // namespace Lighthouse
