#pragma once
#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>
#include <map>

// Observes the real renderer rather than substituting a fake GPU implementation.
class CountingRenderDevice final : public Lighthouse::IRenderDevice
{
  std::unique_ptr<IRenderDevice> owned;
  IRenderDevice& device;
  struct Queue final : Lighthouse::IRenderQueue {
    CountingRenderDevice& owner;
    IRenderQueue& native;
    Queue(CountingRenderDevice& owner,IRenderQueue& native):owner(owner),native(native) {}
    Flags<Lighthouse::RenderQueueCapability> GetCapabilities() const noexcept override {return native.GetCapabilities();}
    void Submit(const Lighthouse::QueueSubmitInfo& info,Lighthouse::FenceHandle fence) override {
      if(info.commandLists.empty()) ++owner.emptySubmissions;
      native.Submit(info,fence);
    }
    void Present(Lighthouse::SwapchainHandle swap,std::span<const Lighthouse::SemaphoreHandle> waits) override {native.Present(swap,waits);}
    void WaitIdle() override {native.WaitIdle();}
  };
  std::map<unsigned,std::unique_ptr<Queue>> queues;
public:
  explicit CountingRenderDevice(std::unique_ptr<IRenderDevice> device):owned(std::move(device)),device(*owned) {}
  unsigned resourceCreations{}, emptySubmissions{};
  bool forceFixedBindings{};
  Lighthouse::RenderCapabilities GetCapabilities() const override {
    auto caps=device.GetCapabilities();
    if(forceFixedBindings) caps.bindlessResources=false;
    return caps;
  }
  Lighthouse::DescriptorSetLayoutHandle CreateDescriptorSetLayout(const Lighthouse::DescriptorSetLayoutDesc& desc) override {
    ++resourceCreations; return device.CreateDescriptorSetLayout(desc);
  }
  Lighthouse::PipelineLayoutHandle CreatePipelineLayout(const Lighthouse::PipelineLayoutDesc& desc) override {
    ++resourceCreations; return device.CreatePipelineLayout(desc);
  }
  std::vector<std::byte> ReadImage(Lighthouse::ImageHandle image) override {return device.ReadImage(image);}
#define FORWARD_CREATE(Name, Result, Desc)                                     \
  Lighthouse::Result Name(const Lighthouse::Desc& desc) override               \
  {                                                                            \
    ++resourceCreations; return device.Name(desc);                                                  \
  }
  FORWARD_CREATE(CreateImage, ImageHandle, ImageDesc)
  FORWARD_CREATE(CreateImageView, ImageViewHandle, ImageViewDesc)
  FORWARD_CREATE(CreateBuffer, BufferHandle, BufferDesc)
  FORWARD_CREATE(CreateShaderModule, ShaderModuleHandle, ShaderModuleDesc)
  FORWARD_CREATE(CreateDescriptorSet, DescriptorSetHandle, DescriptorSetDesc)
  FORWARD_CREATE(CreateSampler, SamplerHandle, SamplerDesc)
  FORWARD_CREATE(CreatePipeline, GraphicsPipelineHandle, GraphicsPipelineDesc)
#undef FORWARD_CREATE
#define FORWARD_VALID(Type)                                                    \
  bool IsValid(Lighthouse::Type handle) const noexcept override                \
  {                                                                            \
    return device.IsValid(handle);                                             \
  }
  FORWARD_VALID(SwapchainHandle)
  FORWARD_VALID(BufferHandle)
  FORWARD_VALID(DescriptorSetLayoutHandle)
  FORWARD_VALID(DescriptorSetHandle)
  FORWARD_VALID(PipelineLayoutHandle)
  FORWARD_VALID(SamplerHandle)
#undef FORWARD_VALID
#define FORWARD_DESTROY(Name, Type)                                            \
  void Name(Lighthouse::Type handle) override                                  \
  {                                                                            \
    device.Name(handle);                                                       \
  }
  FORWARD_DESTROY(DestroyImage, ImageHandle)
  FORWARD_DESTROY(DestroyImageView, ImageViewHandle)
  FORWARD_DESTROY(DestroyBuffer, BufferHandle)
  FORWARD_DESTROY(DestroyPipeline, GraphicsPipelineHandle)
  FORWARD_DESTROY(DestroyModule, ShaderModuleHandle)
  FORWARD_DESTROY(DestroyDescriptorSetLayout, DescriptorSetLayoutHandle)
  FORWARD_DESTROY(DestroyDescriptorSet, DescriptorSetHandle)
  FORWARD_DESTROY(DestroyPipelineLayout, PipelineLayoutHandle)
  FORWARD_DESTROY(DestroySampler, SamplerHandle)
#undef FORWARD_DESTROY
  Lighthouse::SwapchainHandle
  CreateSwapchain(Lighthouse::SurfaceHandle surface,
                  const Lighthouse::SwapchainConfig& config) override
  {
    return device.CreateSwapchain(surface, config);
  }
  Lighthouse::AcquiredSwapchainImage
  AcquireSwapchainImage(Lighthouse::SwapchainHandle h) override
  {
    return device.AcquireSwapchainImage(h);
  }
  bool DestroySwapchain(Lighthouse::SwapchainHandle h) override
  {
    return device.DestroySwapchain(h);
  }
  std::optional<Lighthouse::BufferMetadata>
  GetBufferDesc(Lighthouse::BufferHandle h) const noexcept override
  {
    return device.GetBufferDesc(h);
  }
  void UpdateDescriptorSet(
      Lighthouse::DescriptorSetHandle h,
      std::span<const Lighthouse::DescriptorWrite> writes) override
  {
    device.UpdateDescriptorSet(h, writes);
  }
  std::unique_ptr<Lighthouse::IRenderCommandList>
  CreateCommandList(std::uint32_t queueIndex) override
  {
    return device.CreateCommandList(queueIndex);
  }
  Lighthouse::ImageLayout GetImageLayout(Lighthouse::ImageHandle image) const override {return device.GetImageLayout(image);}
  Lighthouse::SurfaceHandle CreateSurface(Lighthouse::Window& w) override
  {
    return device.CreateSurface(w);
  }
  void DestroySurface(Lighthouse::SurfaceHandle h) override
  {
    device.DestroySurface(h);
  }
  Lighthouse::IRenderQueue* GetQueue(std::uint32_t index) override
  {
    auto* native=device.GetQueue(index);
    if(!native) return nullptr;
    auto& queue=queues[index];
    if(!queue) queue=std::make_unique<Queue>(*this,*native);
    return queue.get();
  }
  Lighthouse::FenceHandle CreateFence(bool signaled) override
  {
    return device.CreateFence(signaled);
  }
  void DestroyFence(Lighthouse::FenceHandle h) override
  {
    return device.DestroyFence(h);
  }
  void ResetFence(Lighthouse::FenceHandle h) override
  {
    return device.ResetFence(h);
  }
  bool GetFenceStatus(Lighthouse::FenceHandle h) override
  {
    return device.GetFenceStatus(h);
  }
  bool WaitForFence(Lighthouse::FenceHandle h, std::uint64_t timeout) override
  {
    return device.WaitForFence(h, timeout);
  }
  Lighthouse::SemaphoreHandle CreateSemaphore(Lighthouse::SemaphoreType type,
                                              std::uint64_t value) override
  {
    return device.CreateSemaphore(type, value);
  }
  void DestroySemaphore(Lighthouse::SemaphoreHandle h) override
  {
    return device.DestroySemaphore(h);
  }
  std::uint64_t GetSemaphoreCounterValue(Lighthouse::SemaphoreHandle h) override
  {
    return device.GetSemaphoreCounterValue(h);
  }
  void SignalSemaphore(Lighthouse::SemaphoreHandle h,
                       std::uint64_t value) override
  {
    return device.SignalSemaphore(h, value);
  }
  bool WaitSemaphore(Lighthouse::SemaphoreHandle h, std::uint64_t value,
                     std::uint64_t timeout) override
  {
    return device.WaitSemaphore(h, value, timeout);
  }
  std::span<std::byte> MapMemory(Lighthouse::BufferHandle h) override
  {
    return device.MapMemory(h);
  }
  void UnmapMemory(Lighthouse::BufferHandle h) override
  {
    return device.UnmapMemory(h);
  }
  void FlushMappedMemoryRange(Lighthouse::BufferHandle h, std::uint64_t offset,
                              std::uint64_t size) override
  {
    return device.FlushMappedMemoryRange(h, offset, size);
  }
  void InvalidateMappedMemoryRange(Lighthouse::BufferHandle h,
                                   std::uint64_t offset,
                                   std::uint64_t size) override
  {
    return device.InvalidateMappedMemoryRange(h, offset, size);
  }
  void WaitIdle() override
  {
    device.WaitIdle();
  }


};
