#include "SceneColorResolver.hpp"
#include <Lighthouse/Engine/Assets/ShaderCompiler.hpp>
#include <Lighthouse/Extended/EmbeddedResources.hpp>
#include <array>
namespace Lighthouse
{
namespace
{
struct ResolveState
{
  IRenderDevice& device;
  DescriptorSetLayoutHandle resolveSetLayout{};
  PipelineLayoutHandle resolveLayout{};
  ShaderModuleHandle resolveVertex{}, resolveFragment{};
  GraphicsPipelineHandle resolvePipeline{};
  SamplerHandle sampler{};
  explicit ResolveState(IRenderDevice& device) : device(device) {}
  ~ResolveState()
  {
    if (resolvePipeline)
      device.DestroyPipeline(resolvePipeline);
    if (resolveVertex)
      device.DestroyModule(resolveVertex);
    if (resolveFragment)
      device.DestroyModule(resolveFragment);
    if (resolveLayout)
      device.DestroyPipelineLayout(resolveLayout);
    if (resolveSetLayout)
      device.DestroyDescriptorSetLayout(resolveSetLayout);
    if (sampler)
      device.DestroySampler(sampler);
  }
  void Initialize()
  {
    if (resolvePipeline)
      return;
    resolveSetLayout = device.CreateDescriptorSetLayout(
        {.bindings = {{.binding = 0,
                       .type = DescriptorType::CombinedImageSampler,
                       .count = 1,
                       .stages = ShaderStageFlags::Fragment}}});
    resolveLayout = device.CreatePipelineLayout({.sets = {resolveSetLayout}});
    resolveVertex = device.CreateShaderModule(ShaderCompiler::Compile(
        ExtendedEmbedded::ReadText("forward/Resolve.vert.glsl"),
        {.stage = ShaderStage::Vertex}));
    resolveFragment = device.CreateShaderModule(ShaderCompiler::Compile(
        ExtendedEmbedded::ReadText("forward/Resolve.frag.glsl"),
        {.stage = ShaderStage::Fragment}));
    resolvePipeline =
        device.CreatePipeline({.vertexShader = resolveVertex,
                               .fragmentShader = resolveFragment,
                               .colorFormats = {ImageFormat::RGBA8},
                               .layout = resolveLayout});
    sampler =
        device.CreateSampler({.addressU = SamplerAddressMode::ClampToEdge,
                              .addressV = SamplerAddressMode::ClampToEdge});
  }
};
} // namespace
void ResolveSceneColor(RenderFeatureContext& frame, RenderFeatureResult& output)
{
  auto state = std::make_shared<ResolveState>(frame.technique.device);
  state->Initialize();
  auto& device = state->device;
  struct SetOwner
  {
    IRenderDevice* device;
    DescriptorSetHandle set;
    SetOwner(IRenderDevice* d, DescriptorSetHandle s) : device(d), set(s) {}
    ~SetOwner()
    {
      device->DestroyDescriptorSet(set);
    }
  };
  for (std::size_t i = 0; i < output.color.size(); ++i)
  {
    const auto source = output.color[i];
    const auto target = frame.destinations[i];
    if (target.encoding != RenderColorEncoding::SRGB)
      throw std::runtime_error(
          "Scene-color resolver currently requires SDR sRGB output");
    struct Data
    {
      RenderGraphResource input, encoded, destination;
    };
    auto set = std::make_shared<SetOwner>(&state->device,
        state->device.CreateDescriptorSet({.layout=state->resolveSetLayout}));
    const auto& pass = frame.technique.graph.AddPass<Data>(
        "Resolve scene view",
        [source, target](RenderGraph::Builder& builder, Data& data)
        {
          data.input = builder.Read(source.image);
          data.encoded = builder.Create<RenderGraphImage>(
              "Resolved SDR color", {.width = source.viewport.width,
                                     .height = source.viewport.height,
                                     .format = ImageFormat::RGBA8,
                                     .usage = {ImageUsage::ColorAttachment,
                                               ImageUsage::TransferSource}});
          data.encoded = builder.Write(data.encoded);
          data.destination = builder.Write(builder.Read(target.image));
        },
        [state, source, target, set](const Data& data,
                                FrameGraphPassResources& resources,
                                RenderGraphExecutePassContext& execute)
        {
          auto& device = state->device;
          const auto& input = resources.get<RenderGraphImage>(data.input);
          const auto& encoded = resources.get<RenderGraphImage>(data.encoded);
          const std::array writes{
              DescriptorWrite{.binding = 0,
                              .resource = DescriptorImageInfo{input.viewHandle,
                                                              state->sampler}}};
          device.UpdateDescriptorSet(set->set, writes);
          auto& commands = *execute.commandList;
          commands.MemoryBarrier(
              {.srcStageMask = PipelineStage::ColorAttachmentOutput,
               .srcAccessMask = PipelineAccess::ColorAttachmentWrite,
               .dstStageMask = PipelineStage::FragmentShader,
               .dstAccessMask = PipelineAccess::ShaderSampledRead});
          commands.BeginRendering({.colorAttachment = encoded.viewHandle,
                                   .width = source.viewport.width,
                                   .height = source.viewport.height});
          commands.BindPipeline(state->resolvePipeline);
          commands.BindDescriptorSet(0, set->set);
          commands.Draw(PrimitiveTopology::TriangleList, 3, 0);
          commands.EndRendering();
          TransitionImage(commands,encoded.handle,encoded.layout,ImageLayout::TransferSource,encoded.subresource);
          ImageHandle destination;
          switch (target.kind)
          {
          case RenderTargetKind::Window:
            destination = resources.get<ImportedWindowView>(data.destination)
                              .currentImage;
            break;
          case RenderTargetKind::ImportedImage:
            destination =
                resources.get<ImportedRenderGraphImage>(data.destination)
                    .handle;
            break;
          case RenderTargetKind::Image:
            destination =
                resources.get<RenderGraphImage>(data.destination).handle;
            break;
          }
          switch(target.kind) {
          case RenderTargetKind::Window: {const auto& image=resources.get<ImportedWindowView>(data.destination);TransitionImage(commands,image.currentImage,image.layout,ImageLayout::TransferDestination);break;}
          case RenderTargetKind::ImportedImage: {const auto& image=resources.get<ImportedRenderGraphImage>(data.destination);TransitionImage(commands,image.handle,image.layout,ImageLayout::TransferDestination,image.subresource);break;}
          case RenderTargetKind::Image: {const auto& image=resources.get<RenderGraphImage>(data.destination);TransitionImage(commands,image.handle,image.layout,ImageLayout::TransferDestination,image.subresource);break;}
          }
          const auto& v = target.viewport;
          commands.MemoryBarrier(
              {.srcStageMask = PipelineStage::ColorAttachmentOutput,
               .srcAccessMask = PipelineAccess::ColorAttachmentWrite,
               .dstStageMask = PipelineStage::Transfer,
               .dstAccessMask = PipelineAccess::TransferRead});
          commands.BlitImage(
              encoded.handle, destination,
              {.srcSubresource = {ImageAspect::Color, 0, 0, 1},
               .srcOffsets = {{{0, 0, 0},
                               {source.viewport.width, source.viewport.height,
                                1}}},
               .dstSubresource = {ImageAspect::Color, target.mipLevel,
                                  target.arrayLayer, 1},
               .dstOffsets = {{{v.x, v.y, 0},
                               {v.x + v.width, v.y + v.height, 1}}}},
              ImageFilter::Linear);
        });
    output.color[i] = target;
    output.color[i].image = pass.destination;
  }
}
} // namespace Lighthouse
