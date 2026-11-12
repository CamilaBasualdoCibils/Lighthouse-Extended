#include <Lighthouse/Extended/Resources/Image.hpp>

namespace Lighthouse
{
ImageData ApplyImageMipPolicy(ImageData image, MipPolicy policy)
{
  image.Validate();
  const bool generate = policy == MipPolicy::Generate ||
                        (policy == MipPolicy::Auto && (image.generateMipmaps || image.mips.size() == 1));
  image.generateMipmaps = generate;
  if (policy == MipPolicy::None || generate) image.mips.resize(1);
  return image;
}
}
