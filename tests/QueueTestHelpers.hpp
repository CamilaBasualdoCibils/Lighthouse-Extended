#pragma once
#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>
inline void SubmitAndWait(Lighthouse::IRenderDevice& device,
                          const Lighthouse::IRenderCommandList& commands)
{
  const auto fence = device.CreateFence();
  try
  {
    device.GetQueue(0)->Submit(commands, fence);
    device.WaitForFence(fence);
  }
  catch (...)
  {
    device.DestroyFence(fence);
    throw;
  }
  device.DestroyFence(fence);
}
