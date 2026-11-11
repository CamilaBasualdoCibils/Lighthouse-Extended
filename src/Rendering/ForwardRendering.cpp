#include <Lighthouse/Engine/Assets/Mesh.hpp>
#include <Lighthouse/Engine/Assets/ShaderCompiler.hpp>
#include <Lighthouse/Extended/EmbeddedResources.hpp>
#include <Lighthouse/Extended/Rendering/ForwardRendering.hpp>
#include <map>
#include <sstream>

namespace Lighthouse
{
ForwardDrawBindings::~ForwardDrawBindings()
{
  if (frameSet)
    device->DestroyDescriptorSet(frameSet);
  if (lightSet)
    device->DestroyDescriptorSet(lightSet);
  if (frame)
    device->DestroyBuffer(frame);
  // MaterialInstance owns the shared parameter set and buffer.
}
struct ForwardRenderServices::Impl
{
  IRenderDevice& device;
  Materials materials;
  std::string lighting, indirect;
  DescriptorSetLayoutHandle indirectLayout{};
  DescriptorSetHandle indirectSet{};
  DescriptorSetLayoutHandle lightsLayout{}, resolveSetLayout{};
  PipelineLayoutHandle resolveLayout{};
  ShaderModuleHandle resolveVertex{}, resolveFragment{};
  GraphicsPipelineHandle resolvePipeline{};
  SamplerHandle sampler{};
  struct Pipeline
  {
    IRenderDevice* device{};
    std::shared_ptr<const Material> material;
    PipelineLayoutHandle layout{};
    ShaderModuleHandle vertex{}, fragment{};
    GraphicsPipelineHandle pipeline{};
    ~Pipeline()
    {
      if (pipeline)
        device->DestroyPipeline(pipeline);
      if (vertex)
        device->DestroyModule(vertex);
      if (fragment)
        device->DestroyModule(fragment);
      if (layout)
        device->DestroyPipelineLayout(layout);
    }
  };
  std::map<std::string, std::shared_ptr<Pipeline>> pipelines;
  std::map<std::shared_ptr<SceneMaterialGPU>,
           std::shared_ptr<const MaterialInstance>>
      instances;
  explicit Impl(IRenderDevice& d, std::string source)
      : device(d), materials(d), lighting(std::move(source))
  {
  }
  ~Impl()
  {
    if (resolvePipeline)
      device.DestroyPipeline(resolvePipeline);
    for (auto shader : {resolveVertex, resolveFragment})
      if (shader)
        device.DestroyModule(shader);
    if (resolveLayout)
      device.DestroyPipelineLayout(resolveLayout);
    pipelines.clear();
    instances.clear();
    for (auto set : {lightsLayout, resolveSetLayout})
      if (set)
        device.DestroyDescriptorSetLayout(set);
    if (sampler)
      device.DestroySampler(sampler);
  }
  void Initialize()
  {
    if (!lightsLayout)
      lightsLayout = device.CreateDescriptorSetLayout(
          {.bindings = {{.binding = 0,
                         .type = DescriptorType::StorageBuffer,
                         .count = 1,
                         .stages = ShaderStageFlags::Fragment}}});
  }
  void InitializeResolve()
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
  std::shared_ptr<const MaterialInstance>
  Instance(std::shared_ptr<SceneMaterialGPU> source)
  {
    if (const auto found = instances.find(source); found != instances.end())
      return found->second;
    const auto definition = materials.Load(source->definition);
    auto instance = definition->Instantiate(source->options);
    instances.emplace(std::move(source), instance);
    return instance;
  }
};
ForwardRenderServices::ForwardRenderServices(IRenderDevice& device,
                                             std::string lighting,
                                             std::string indirect,
                                             DescriptorSetLayoutHandle indirectLayout,
                                             DescriptorSetHandle indirectSet)
    : m_impl(std::make_unique<Impl>(device, std::move(lighting)))
{
  m_impl->indirect = std::move(indirect);
  m_impl->indirectLayout = indirectLayout;
  m_impl->indirectSet = indirectSet;
}
ForwardRenderServices::~ForwardRenderServices() = default;
GraphicsPipelineHandle
ForwardRenderServices::GetPipeline(const VertexLayout& layout,
                                   std::shared_ptr<SceneMaterialGPU> source)
{
  m_impl->Initialize();
  const auto material = m_impl->Instance(source)->material;
  const bool blend = source->data.alphaMode == MaterialAsset::AlphaMode::Blend;
  std::ostringstream key;
  key << material->path.generic_string() << ':' << blend;
  for (const auto& b : layout.bindings)
    key << ':' << b.binding << ',' << b.stride << ',' << int(b.inputRate);
  for (const auto& a : layout.attributes)
    key << ':' << a.location << ',' << a.binding << ',' << int(a.format) << ','
        << a.offset;
  if (const auto found = m_impl->pipelines.find(key.str());
      found != m_impl->pipelines.end())
    return found->second->pipeline;
  auto pipeline = std::make_shared<Impl::Pipeline>();
  pipeline->device = &m_impl->device;
  pipeline->material = material;
  const auto compiled =
      material->Compile(MaterialRenderPath::Forward, m_impl->lighting, m_impl->indirect);
  pipeline->vertex = m_impl->device.CreateShaderModule(*compiled.vertex);
  pipeline->fragment = m_impl->device.CreateShaderModule(*compiled.fragment);
  std::vector<DescriptorSetLayoutHandle> sets{material->frameLayout, material->parametersLayout, m_impl->lightsLayout};
  if (m_impl->indirectLayout) sets.push_back(m_impl->indirectLayout);
  pipeline->layout = m_impl->device.CreatePipelineLayout({.sets = sets});
  GraphicsPipelineDesc desc{.vertexShader = pipeline->vertex,
                            .fragmentShader = pipeline->fragment,
                            .vertexLayout = layout,
                            .colorFormats = {ImageFormat::RGBA16f},
                            .depthFormat = ImageFormat::Depth32,
                            .layout = pipeline->layout};
  if (blend)
    desc.blend = {.enabled = true,
                  .sourceColorFactor = BlendFactor::SourceAlpha,
                  .destinationColorFactor = BlendFactor::OneMinusSourceAlpha,
                  .sourceAlphaFactor = BlendFactor::One,
                  .destinationAlphaFactor = BlendFactor::OneMinusSourceAlpha};
  pipeline->pipeline = m_impl->device.CreatePipeline(desc);
  m_impl->pipelines.emplace(key.str(), pipeline);
  return pipeline->pipeline;
}
std::shared_ptr<ForwardDrawBindings> ForwardRenderServices::CreateBindings(
    std::shared_ptr<SceneMaterialGPU> material, BufferHandle lights)
{
  m_impl->Initialize();
  auto& device = m_impl->device;
  auto result = std::make_shared<ForwardDrawBindings>();
  result->device = &device;
  result->indirectSet = m_impl->indirectSet;
  result->source = std::move(material);
  result->instance = m_impl->Instance(result->source);
  result->frame = device.CreateBuffer(
      {.size = sizeof(ForwardFrameData),
       .usage = BufferUsage::Uniform,
       .memory = {.requiredAccess =
                      MemoryAccess::CPUWrite | MemoryAccess::GPURead}});
  result->material = result->instance->parametersBuffer;
  result->materialSet = result->instance->parametersSet;
  result->frameSet = device.CreateDescriptorSet(
      {.layout = result->instance->material->frameLayout});
  const std::array frameWrites{DescriptorWrite{
      .binding = 0,
      .resource =
          DescriptorBufferInfo{result->frame, 0, sizeof(ForwardFrameData)}}};
  device.UpdateDescriptorSet(result->frameSet, frameWrites);
  if (lights)
  {
    result->lightSet =
        device.CreateDescriptorSet({.layout = m_impl->lightsLayout});
    const auto size = device.GetBufferDesc(lights)->size;
    const std::array lightWrites{DescriptorWrite{
        .binding = 0, .resource = DescriptorBufferInfo{lights, 0, size}}};
    device.UpdateDescriptorSet(result->lightSet, lightWrites);
  }
  return result;
}
void ForwardRenderServices::BeginGeometry(ForwardRenderContext& ctx)
{
  if (!ctx.output.color.empty() &&
      ctx.output.color.front().encoding == RenderColorEncoding::SceneLinear)
    return;
  RenderOutputTargets colors, depths;
  for (const auto& view : ctx.frame.family->views)
  {
    struct Data
    {
      RenderGraphResource color, depth;
    };
    const auto size = view.resolution;
    const auto& pass = ctx.frame.technique.graph.AddPass<Data>(
        "Forward view attachments",
        [size](RenderGraph::Builder& builder, Data& data)
        {
          data.color = builder.Write(builder.Create<RenderGraphImage>(
              "Forward HDR color",
              {.width = size.x,
               .height = size.y,
               .format = ImageFormat::RGBA16f,
               .usage = {ImageUsage::ColorAttachment, ImageUsage::Sampled}}));
          data.depth = builder.Write(builder.Create<RenderGraphImage>(
              "Forward depth", {.width = size.x,
                                .height = size.y,
                                .format = ImageFormat::Depth32,
                                .usage = {ImageUsage::DepthStencilAttachment,
                                          ImageUsage::Sampled}}));
        },
        [size](const Data& data, FrameGraphPassResources& resources,
               RenderGraphExecutePassContext& execute)
        {
          auto& commands = *execute.commandList;
          commands.BeginRendering(
              {.colorAttachment =
                   resources.get<RenderGraphImage>(data.color).viewHandle,
               .depthAttachment =
                   resources.get<RenderGraphImage>(data.depth).viewHandle,
               .clearColor = true,
               .clearColorValue = {0.015f, 0.02f, 0.03f},
               .clearDepth = true,
               .width = size.x,
               .height = size.y});
          commands.EndRendering();
        });
    colors.push_back({view.id,
                      pass.color,
                      0,
                      0,
                      {0, 0, size.x, size.y},
                      ImageFormat::RGBA16f,
                      RenderColorEncoding::SceneLinear});
    depths.push_back({view.id,
                      pass.depth,
                      0,
                      0,
                      {0, 0, size.x, size.y},
                      ImageFormat::Depth32,
                      RenderColorEncoding::SceneLinear});
  }
  ctx.output.color = std::move(colors);
  ctx.output.auxiliary[ForwardSemantics::Depth] = std::move(depths);
}
void ForwardRenderServices::Resolve(RenderFeatureContext& frame,
                                    RenderFeatureResult& output)
{
  m_impl->InitializeResolve();
  auto& device = m_impl->device;
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
          "Default forward resolver currently requires SDR sRGB output");
    auto set = std::make_shared<SetOwner>(
        &device,
        device.CreateDescriptorSet({.layout = m_impl->resolveSetLayout}));
    struct Data
    {
      RenderGraphResource input, encoded, destination;
    };
    const auto& pass = frame.technique.graph.AddPass<Data>(
        "Resolve forward view",
        [source, target](RenderGraph::Builder& builder, Data& data)
        {
          data.input = builder.Read(source.image);
          data.encoded = builder.Create<RenderGraphImage>(
              "Forward SDR color", {.width = source.viewport.width,
                                    .height = source.viewport.height,
                                    .format = ImageFormat::RGBA8,
                                    .usage = {ImageUsage::ColorAttachment,
                                              ImageUsage::TransferSource}});
          data.encoded = builder.Write(data.encoded);
          data.destination = builder.Write(builder.Read(target.image));
        },
        [this, set, source, target](const Data& data,
                                    FrameGraphPassResources& resources,
                                    RenderGraphExecutePassContext& execute)
        {
          auto& device = m_impl->device;
          const auto& input = resources.get<RenderGraphImage>(data.input);
          const auto& encoded = resources.get<RenderGraphImage>(data.encoded);
          const std::array writes{DescriptorWrite{
              .binding = 0,
              .resource =
                  DescriptorImageInfo{input.viewHandle, m_impl->sampler}}};
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
          commands.BindPipeline(m_impl->resolvePipeline);
          commands.BindDescriptorSet(0, set->set);
          commands.Draw(PrimitiveTopology::TriangleList, 3, 0);
          commands.EndRendering();
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
