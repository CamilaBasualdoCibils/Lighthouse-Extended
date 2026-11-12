#pragma once
#include <Lighthouse/Engine/Assets/ImageData.hpp>
#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>

namespace Lighthouse::Detail
{
// Resource creation already uploads synchronously. Finish ordinary mip generation
// once at upload, before publishing the image to materials or IBL filtering.
inline void GenerateUploadedMipmaps(IRenderDevice& device, ImageHandle image, const ImageData& pixels)
{
  if (!pixels.generateMipmaps || pixels.GetRequiredMipCount() <= 1) return;
  auto commands = device.CreateCommandList(0);
  commands->Begin();
  commands->GenMipMaps(GenMipMapsDesc{.image=image});
  commands->End();
  const auto fence = device.CreateFence();
  try {
    device.GetQueue(0)->Submit(*commands, fence);
    if (!device.WaitForFence(fence)) throw std::runtime_error("Mipmap generation wait failed");
  } catch (...) {
    device.WaitIdle();
    device.DestroyFence(fence);
    throw;
  }
  device.DestroyFence(fence);
}
}
