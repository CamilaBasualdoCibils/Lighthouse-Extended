#pragma once
#include "../extern/Lighthouse-Engine/Tests/NullSynchronizationDevice.hpp"
using namespace Lighthouse;
class TestRenderDevice final : public NullSynchronizationDevice {
public:
    SwapchainHandle CreateSwapchain(SurfaceHandle, const SwapchainConfig&) override { return {}; }
    AcquiredSwapchainImage AcquireSwapchainImage(SwapchainHandle) override { return {}; }
    bool DestroySwapchain(SwapchainHandle) override { return false; }
    bool IsValid(SwapchainHandle) const noexcept override { return false; }
    ImageHandle CreateImage(const ImageDesc&) override { return {1, 1, DeviceId()}; }
    ImageViewHandle CreateImageView(const ImageViewDesc&) override { return {}; }
    BufferHandle CreateBuffer(const BufferDesc&) override { return {}; }
    std::optional<BufferMetadata> GetBufferDesc(BufferHandle) const noexcept override { return {}; }
    bool IsValid(BufferHandle) const noexcept override { return false; }
    bool IsValid(DescriptorSetLayoutHandle) const noexcept override { return false; }
    bool IsValid(DescriptorSetHandle) const noexcept override { return false; }
    bool IsValid(PipelineLayoutHandle) const noexcept override { return false; }
    bool IsValid(SamplerHandle) const noexcept override { return false; }
    ShaderModuleHandle CreateShaderModule(const ShaderModuleDesc&) override { return {}; }
    DescriptorSetLayoutHandle CreateDescriptorSetLayout(const DescriptorSetLayoutDesc&) override { return {}; }
    DescriptorSetHandle CreateDescriptorSet(const DescriptorSetDesc&) override { return {}; }
    PipelineLayoutHandle CreatePipelineLayout(const PipelineLayoutDesc&) override { return {}; }
    SamplerHandle CreateSampler(const SamplerDesc&) override { return {}; }
    void UpdateDescriptorSet(DescriptorSetHandle, std::span<const DescriptorWrite>) override {}
    GraphicsPipelineHandle CreatePipeline(const GraphicsPipelineDesc&) override { return {}; }
    std::unique_ptr<IRenderCommandList>
    CreateCommandList(std::uint32_t queueIndex) override
    {
      return {};
    }
    void DestroyImage(ImageHandle) override {}
    void DestroyImageView(ImageViewHandle) override {}
    void DestroyBuffer(BufferHandle) override {}
    void DestroyPipeline(GraphicsPipelineHandle) override {}
    void DestroyModule(ShaderModuleHandle) override {}
    void DestroyDescriptorSetLayout(DescriptorSetLayoutHandle) override {}
    void DestroyDescriptorSet(DescriptorSetHandle) override {}
    void DestroyPipelineLayout(PipelineLayoutHandle) override {}
    void DestroySampler(SamplerHandle) override {}
    RenderCapabilities GetCapabilities() const override { return {}; }
    void WaitIdle() override {}
};

