#include <Lighthouse/Extended/World/HostBuffer.hpp>
#include <Lighthouse/Engine/Assets/ShaderCompiler.hpp>
#include <Lighthouse/Extended/EmbeddedResources.hpp>
#include <Lighthouse/Extended/Rendering/Bloom.hpp>
#include <algorithm>
#include <array>
#include <cmath>
namespace Lighthouse
{
Bloom::Bloom(BloomSettings settings)
    : m_settings(std::make_shared<BloomSettings>())
{
  SetSettings(settings);
}
const BloomSettings& Bloom::GetSettings() const noexcept
{
  return *m_settings;
}
void Bloom::SetSettings(BloomSettings settings)
{
  if (!std::isfinite(settings.Threshold) || settings.Threshold < 0 ||
      !std::isfinite(settings.SoftKnee) || settings.SoftKnee < 0 ||
      settings.SoftKnee > 1 || !std::isfinite(settings.Intensity) ||
      settings.Intensity < 0 || !settings.MipCount)
    throw std::invalid_argument("Invalid bloom settings");
  *m_settings = settings;
}
std::uint64_t Bloom::GetConfigurationRevision() const noexcept
{
  return (std::uint64_t(m_settings->MipCount) << 1) | m_settings->Enabled;
}
void Bloom::BuildGraphForward(ForwardRenderContext& context)
{
  BuildBloom(context.frame, context.output);
}
void Bloom::BuildGraphDeferred(DeferredRenderContext& context)
{
  BuildBloom(context.frame, context.output);
}
namespace
{
struct BloomGPU
{
  IRenderDevice& device;
  DescriptorSetLayoutHandle setLayout{};
  PipelineLayoutHandle layout{};
  ShaderModuleHandle vertex{}, fragment{};
  GraphicsPipelineHandle pipeline{};
  SamplerHandle sampler{};
  explicit BloomGPU(IRenderDevice& d) : device(d) {}
  void Initialize()
  {
    setLayout = device.CreateDescriptorSetLayout(
        {.bindings = {{.binding = 0,
                       .type = DescriptorType::CombinedImageSampler,
                       .count = 1,
                       .stages = ShaderStageFlags::Fragment},
                      {.binding = 1,
                       .type = DescriptorType::CombinedImageSampler,
                       .count = 1,
                       .stages = ShaderStageFlags::Fragment},
                      {.binding = 2,
                       .type = DescriptorType::UniformBuffer,
                       .count = 1,
                       .stages = ShaderStageFlags::Fragment}}});
    layout = device.CreatePipelineLayout({.sets = {setLayout}});
    vertex = device.CreateShaderModule(ShaderCompiler::Compile(
        ExtendedEmbedded::ReadText("forward/Resolve.vert.glsl"),
        {.stage = ShaderStage::Vertex}));
    fragment = device.CreateShaderModule(ShaderCompiler::Compile(
        ExtendedEmbedded::ReadText("bloom/Filter.frag.glsl"),
        {.stage = ShaderStage::Fragment}));
    pipeline = device.CreatePipeline({.vertexShader = vertex,
                                      .fragmentShader = fragment,
                                      .colorFormats = {ImageFormat::RGBA16f},
                                      .layout = layout});
    sampler =
        device.CreateSampler({.addressU = SamplerAddressMode::ClampToEdge,
                              .addressV = SamplerAddressMode::ClampToEdge});
  }
  ~BloomGPU()
  {
    if (pipeline)
      device.DestroyPipeline(pipeline);
    if (vertex)
      device.DestroyModule(vertex);
    if (fragment)
      device.DestroyModule(fragment);
    if (layout)
      device.DestroyPipelineLayout(layout);
    if (setLayout)
      device.DestroyDescriptorSetLayout(setLayout);
    if (sampler)
      device.DestroySampler(sampler);
  }
};
struct BloomPass
{
  std::shared_ptr<BloomGPU> gpu;
  BufferHandle parameters{};
  DescriptorSetHandle set{};
  std::vector<std::tuple<ImageHandle,uint32_t,uint32_t,ImageViewHandle>> views;
  ~BloomPass()
  {
    if (set)
      gpu->device.DestroyDescriptorSet(set);
    if (parameters)
      gpu->device.DestroyBuffer(parameters);
    for(auto [image,mip,layer,view] : views) gpu->device.DestroyImageView(view);
  }
};
struct alignas(16) BloomParameters
{
  Vec4 settings, region, outputSize;
};
} // namespace
void Bloom::BuildBloom(RenderFeatureContext& frame, RenderFeatureResult& output)
{
  if (!m_settings->Enabled || output.color.empty())
    return;
  for (const auto& source : output.color)
    if (source.encoding != RenderColorEncoding::SceneLinear ||
        source.kind == RenderTargetKind::Window)
      throw std::logic_error(
          "Bloom requires a sampled HDR linear image before output resolution");
  auto gpu = std::make_shared<BloomGPU>(frame.technique.device);
  gpu->Initialize();
  auto& graph = frame.technique.graph;
  auto addPass = [&](std::string name, RenderOutputTarget source,
                     RenderOutputTarget bloom, UVec2 size, int operation)
  {
    std::uint32_t sourceWidth, sourceHeight;
    if (source.kind == RenderTargetKind::ImportedImage)
    {
      const auto& desc =
          graph.getDescriptor<ImportedRenderGraphImage>(source.image);
      sourceWidth = desc.width;
      sourceHeight = desc.height;
    }
    else
    {
      const auto& desc = graph.getDescriptor<RenderGraphImage>(source.image);
      sourceWidth = desc.width;
      sourceHeight = desc.height;
    }
    sourceWidth = std::max(1u, sourceWidth >> source.mipLevel);
    sourceHeight = std::max(1u, sourceHeight >> source.mipLevel);
    const Vec4 region(float(source.viewport.width) / sourceWidth,
                      float(source.viewport.height) / sourceHeight,
                      float(source.viewport.x) / sourceWidth,
                      float(source.viewport.y) / sourceHeight);
    struct Data
    {
      RenderGraphResource source, bloom, output;
    };
    auto state=std::make_shared<BloomPass>();
    state->gpu=gpu;
    const auto& data = graph.AddPass<Data>(
        name,
        [=](RenderGraph::Builder& builder, Data& data)
        {
          data.source = builder.Read(source.image);
          data.bloom = builder.Read(bloom.image);
          data.output = builder.Write(builder.Create<RenderGraphImage>(
              name + " color",
              {.width = size.x,
               .height = size.y,
               .format = ImageFormat::RGBA16f,
               .usage = {ImageUsage::ColorAttachment, ImageUsage::Sampled}}));
        },
        [gpuState = gpu, settings = m_settings, source, bloom, size, region,
         operation, state](const Data& data, FrameGraphPassResources& resources,
                    RenderGraphExecutePassContext& execute)
        {
          auto& gpu = *gpuState;
          auto sampleView =
              [&](RenderOutputTarget target, RenderGraphResource resource)
          {
            ImageHandle image;
            ImageViewHandle view;
            bool needsView = target.arrayLayer || target.mipLevel;
            if (target.kind == RenderTargetKind::ImportedImage)
            {
              const auto& value =
                  resources.get<ImportedRenderGraphImage>(resource);
              image = value.handle;
              view = value.viewHandle;
              needsView = true;
            }
            else
            {
              const auto& value = resources.get<RenderGraphImage>(resource);
              image = value.handle;
              view = value.viewHandle;
              needsView |= value.subresource.layerCount > 1 ||
                           value.subresource.mipLevelCount > 1;
            }
            if (needsView)
            {
              for(auto [cachedImage,mip,layer,cachedView] : state->views)
                if(cachedImage==image && mip==target.mipLevel && layer==target.arrayLayer) return cachedView;
              view = gpu.device.CreateImageView(
                  {.image = image,
                   .format = target.format,
                   .baseMipLevel = target.mipLevel,
                   .baseArrayLayer = target.arrayLayer});
              state->views.emplace_back(image,target.mipLevel,target.arrayLayer,view);
            }
            return view;
          };
          const BloomParameters parameters{
              {settings->Threshold, settings->SoftKnee, settings->Intensity,
               float(operation)},
              region,
              {1.0f / size.x, 1.0f / size.y, 0, 0}};
          if(!state->parameters) state->parameters = gpu.device.CreateBuffer(
              {.size = sizeof(BloomParameters),
               .usage = BufferUsage::Uniform,
               .initialData = std::as_bytes(std::span(&parameters, 1)),
               .memory = {.requiredAccess =
                              MemoryAccess::CPUWrite | MemoryAccess::GPURead}});
          if(!state->set) state->set = gpu.device.CreateDescriptorSet({.layout = gpu.setLayout});
          WriteRetiredHostBuffer(gpu.device,state->parameters,std::as_bytes(std::span(&parameters,1)));
          const std::array writes{
              DescriptorWrite{
                  .binding = 0,
                  .resource =
                      DescriptorImageInfo{sampleView(source, data.source),
                                          gpu.sampler}},
              DescriptorWrite{
                  .binding = 1,
                  .resource = DescriptorImageInfo{sampleView(bloom, data.bloom),
                                                  gpu.sampler}},
              DescriptorWrite{
                  .binding = 2,
                  .resource = DescriptorBufferInfo{state->parameters, 0,
                                                   sizeof(BloomParameters)}}};
          gpu.device.UpdateDescriptorSet(state->set, writes);
          auto& commands = *execute.commandList;
          commands.MemoryBarrier(
              {.srcStageMask = PipelineStage::ColorAttachmentOutput,
               .srcAccessMask = PipelineAccess::ColorAttachmentWrite,
               .dstStageMask = PipelineStage::FragmentShader,
               .dstAccessMask = PipelineAccess::ShaderSampledRead});
          commands.BeginRendering(
              {.colorAttachment =
                   resources.get<RenderGraphImage>(data.output).viewHandle,
               .width = size.x,
               .height = size.y});
          commands.BindPipeline(gpu.pipeline);
          commands.BindDescriptorSet(0, state->set);
          commands.Draw(PrimitiveTopology::TriangleList, 3, 0);
          commands.EndRendering();
        });
    return RenderOutputTarget{source.view,
                              data.output,
                              0,
                              0,
                              {0, 0, size.x, size.y},
                              ImageFormat::RGBA16f,
                              RenderColorEncoding::SceneLinear};
  };
  for (auto& source : output.color)
  {
    const UVec2 fullSize{source.viewport.width, source.viewport.height};
    if (!fullSize.x || !fullSize.y)
      throw std::logic_error("Bloom requires a nonempty viewport");
    std::vector<RenderOutputTarget> levels;
    auto input = source;
    auto size = fullSize;
    for (std::uint32_t level = 0; level < m_settings->MipCount; ++level)
    {
      size = {std::max(1u, (size.x + 1) / 2), std::max(1u, (size.y + 1) / 2)};
      input = addPass(level ? "Bloom downsample" : "Bloom highlight extraction",
                      input, input, size, level ? 1 : 0);
      levels.push_back(input);
      if (size.x == 1 && size.y == 1)
        break;
    }
    auto reconstructed = levels.back();
    for (std::size_t i = levels.size() - 1; i > 0; --i)
    {
      const auto high = levels[i - 1];
      reconstructed = addPass("Bloom upsample", high, reconstructed,
                              {high.viewport.width, high.viewport.height}, 2);
    }
    source = addPass("Bloom composite", source, reconstructed, fullSize, 3);
  }
}
} // namespace Lighthouse
