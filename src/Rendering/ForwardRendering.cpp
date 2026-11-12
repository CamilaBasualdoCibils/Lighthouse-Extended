#include "SceneColorResolver.hpp"
#include <Lighthouse/Engine/Assets/Mesh.hpp>
#include <Lighthouse/Engine/Assets/ShaderCompiler.hpp>
#include <Lighthouse/Extended/EmbeddedResources.hpp>
#include <Lighthouse/Extended/Rendering/ForwardRendering.hpp>
#include <map>
#include <sstream>

namespace Lighthouse
{
struct ForwardRenderServices::Impl
{
  IRenderDevice& device;
  Materials materials;
  std::string lighting, indirect;
  DescriptorSetLayoutHandle indirectLayout{};
  DescriptorSetHandle indirectSet{};
  DescriptorSetLayoutHandle lightsLayout{};
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
    pipelines.clear();
    instances.clear();
    if(lightsLayout)device.DestroyDescriptorSetLayout(lightsLayout);
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
  std::shared_ptr<const MaterialInstance>
  Instance(std::shared_ptr<SceneMaterialGPU> source)
  {
    if (const auto found = instances.find(source); found != instances.end())
      return found->second;
    const auto definition = materials.Load(source->definition);
    auto instance = definition->Instantiate(source->options, !device.GetCapabilities().bindlessResources);
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
                                   std::shared_ptr<SceneMaterialGPU> source, const SceneBindings& bindings)
{
  m_impl->Initialize();
  const auto material = m_impl->Instance(source)->material;
  const bool blend = source->data.alphaMode == MaterialAsset::AlphaMode::Blend;
  std::ostringstream key;
  key << material->path.generic_string() << ':' << blend << ':' << bindings.FrameLayout().index << ':' << bindings.AssetLayout().index;
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
      material->Compile(MaterialRenderPath::Forward, m_impl->lighting, m_impl->indirect, {}, bindings.ShaderOptions());
  pipeline->vertex = m_impl->device.CreateShaderModule(*compiled.vertex);
  pipeline->fragment = m_impl->device.CreateShaderModule(*compiled.fragment);
  std::vector<DescriptorSetLayoutHandle> sets{bindings.FrameLayout(), bindings.ShaderOptions().bindless ? bindings.AssetLayout() : material->parametersLayout, m_impl->lightsLayout};
  if (m_impl->indirectLayout) sets.push_back(m_impl->indirectLayout);
  pipeline->layout = m_impl->device.CreatePipelineLayout({.sets = sets,.drawIndex=true});
  GraphicsPipelineDesc desc{.vertexShader = pipeline->vertex,
                            .fragmentShader = pipeline->fragment,
                            .vertexLayout = layout,
                            .colorFormats = {ImageFormat::RGBA16f},
                            .depthFormat = ImageFormat::Depth32,
                            .layout = pipeline->layout,.depthStencil={.depthTest=true,.depthWrite=true}};
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
std::shared_ptr<const MaterialInstance> ForwardRenderServices::GetMaterial(std::shared_ptr<SceneMaterialGPU> material)
{
  return m_impl->Instance(std::move(material));
}
DescriptorSetHandle ForwardRenderServices::IndirectSet() const { return m_impl->indirectSet; }
DescriptorSetHandle ForwardRenderServices::CreateLightingSet(BufferHandle lights)
{
  m_impl->Initialize();
  if (!lights) return {};
  auto& device=m_impl->device;
  const auto set=device.CreateDescriptorSet({.layout=m_impl->lightsLayout});
  const std::array writes{DescriptorWrite{.binding=0,.resource=DescriptorBufferInfo{lights}}};
  device.UpdateDescriptorSet(set,writes);
  return set;
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
void ForwardRenderServices::Resolve(RenderFeatureContext& frame, RenderFeatureResult& output)
{
  ResolveSceneColor(frame, output);
}

} // namespace Lighthouse
