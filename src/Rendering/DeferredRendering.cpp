#include <Lighthouse/Engine/Assets/ShaderCompiler.hpp>
#include <Lighthouse/Extended/EmbeddedResources.hpp>
#include <Lighthouse/Extended/Rendering/DeferredRendering.hpp>
#include <cstring>
#include <set>
#include <sstream>
namespace Lighthouse
{
namespace
{
GBufferAttribute Attribute(DeferredSemantics semantic)
{
  switch (semantic)
  {
  case DeferredSemantics::Albedo:
    return GBufferAttribute::Color;
  case DeferredSemantics::Normal:
    return GBufferAttribute::WorldNormal;
  case DeferredSemantics::WorldPosition:
    return GBufferAttribute::WorldPosition;
  case DeferredSemantics::SurfaceProperties:
    return GBufferAttribute::SurfaceProperties;
  case DeferredSemantics::Emission:
    return GBufferAttribute::Emission;
  case DeferredSemantics::Motion:
    return GBufferAttribute::Motion;
  case DeferredSemantics::Metallic:
    return GBufferAttribute::Metallic;
  case DeferredSemantics::Roughness:
    return GBufferAttribute::Roughness;
  case DeferredSemantics::Depth:
    break;
  }
  throw std::logic_error("Depth is not a deferred color attachment");
}
struct Pipeline
{
  IRenderDevice& device;
  ShaderModuleHandle vertex{}, fragment{};
  PipelineLayoutHandle layout{};
  GraphicsPipelineHandle pipeline{};
  std::shared_ptr<const Material> material;
  explicit Pipeline(IRenderDevice& d) : device(d) {}
  ~Pipeline()
  {
    if (pipeline)
      device.DestroyPipeline(pipeline);
    if (vertex)
      device.DestroyModule(vertex);
    if (fragment)
      device.DestroyModule(fragment);
    if (layout)
      device.DestroyPipelineLayout(layout);
  }
};
struct ShadeState
{
  IRenderDevice& device;
  DescriptorSetLayoutHandle images{}, lights{};
  std::shared_ptr<Pipeline> pipeline;
  SamplerHandle sampler{};
  explicit ShadeState(IRenderDevice& d) : device(d) {}
  ~ShadeState()
  {
    pipeline.reset();
    if (images)
      device.DestroyDescriptorSetLayout(images);
    if (lights)
      device.DestroyDescriptorSetLayout(lights);
    if (sampler)
      device.DestroySampler(sampler);
  }
};
struct ShadeBindings
{
  std::shared_ptr<ShadeState> state;
  DescriptorSetHandle images{}, lights{};
  BufferHandle buffer{};
  ~ShadeBindings()
  {
    auto& d = state->device;
    if (images)
      d.DestroyDescriptorSet(images);
    if (lights)
      d.DestroyDescriptorSet(lights);
    if (buffer)
      d.DestroyBuffer(buffer);
  }
};
} // namespace
struct DeferredRenderServices::Impl
{
  IRenderDevice& device;
  Materials materials;
  std::vector<GBufferAttribute> attributes;
  std::map<std::shared_ptr<SceneMaterialGPU>,
           std::shared_ptr<const MaterialInstance>>
      instances;
  std::map<std::string, std::shared_ptr<Pipeline>> pipelines;
  DeferredDependencies requirements;
  Impl(IRenderDevice& d, DeferredDependencies requested)
      : device(d), materials(d), requirements(std::move(requested))
  {
    std::set<GBufferAttribute> selected{
        GBufferAttribute::Color, GBufferAttribute::WorldNormal,
        GBufferAttribute::WorldPosition, GBufferAttribute::SurfaceProperties,
        GBufferAttribute::Emission};
    for (auto semantic : requirements.attachments)
      if (semantic != DeferredSemantics::Depth)
        selected.insert(Attribute(semantic));
    attributes.assign(selected.begin(), selected.end());
  }
};
DeferredRenderServices::DeferredRenderServices(IRenderDevice& d,
                                               DeferredDependencies requested)
    : m_impl(std::make_unique<Impl>(d, std::move(requested)))
{
}
DeferredRenderServices::~DeferredRenderServices() = default;
const std::vector<GBufferAttribute>& DeferredRenderServices::Attributes() const
{
  return m_impl->attributes;
}
std::shared_ptr<const MaterialInstance>
DeferredRenderServices::GetMaterial(std::shared_ptr<SceneMaterialGPU> source)
{
  if (auto it = m_impl->instances.find(source); it != m_impl->instances.end())
    return it->second;
  auto instance =
      m_impl->materials.Load(source->definition)->Instantiate(source->options);
  m_impl->instances.emplace(source, instance);
  return instance;
}
GraphicsPipelineHandle
DeferredRenderServices::GetPipeline(const VertexLayout& vertexLayout,
                                    std::shared_ptr<SceneMaterialGPU> source)
{
  auto instance = GetMaterial(source);
  std::ostringstream key;
  key << instance->material->path.generic_string();
  for (auto a : vertexLayout.attributes)
    key << ':' << a.location << ',' << int(a.format) << ',' << a.offset;
  for (auto b : vertexLayout.bindings)
    key << ':' << b.binding << ',' << b.stride << ',' << int(b.inputRate);
  if (auto it = m_impl->pipelines.find(key.str());
      it != m_impl->pipelines.end())
    return it->second->pipeline;
  auto pipeline = std::make_shared<Pipeline>(m_impl->device);
  pipeline->material = instance->material;
  const auto shaders = instance->material->Compile(MaterialRenderPath::Deferred,
                                                   {}, {}, m_impl->attributes);
  pipeline->vertex = m_impl->device.CreateShaderModule(*shaders.vertex);
  pipeline->fragment = m_impl->device.CreateShaderModule(*shaders.fragment);
  pipeline->layout = m_impl->device.CreatePipelineLayout(
      {.sets = {instance->material->frameLayout,
                instance->material->parametersLayout}});
  std::vector<ImageFormat> formats;
  for (auto attribute : m_impl->attributes)
    formats.push_back(GBufferAttributes[std::size_t(attribute)].format);
  pipeline->pipeline =
      m_impl->device.CreatePipeline({.vertexShader = pipeline->vertex,
                                     .fragmentShader = pipeline->fragment,
                                     .vertexLayout = vertexLayout,
                                     .colorFormats = formats,
                                     .depthFormat = ImageFormat::Depth32,
                                     .layout = pipeline->layout});
  m_impl->pipelines.emplace(key.str(), pipeline);
  return pipeline->pipeline;
}
void DeferredRenderServices::BeginGeometry(DeferredRenderContext& context)
{
  if (!context.output.color.empty() &&
      context.output.color.front().encoding == RenderColorEncoding::SceneLinear)
    return;
  context.gbuffers.resize(context.frame.family->views.size());
  context.output.color.clear();
  for (std::size_t view = 0; view < context.frame.family->views.size(); ++view)
  {
    const auto& camera = context.frame.family->views[view];
    struct Data
    {
      std::map<GBufferAttribute, RenderGraphResource> attributes;
      RenderGraphResource depth, color;
    };
    const auto& data = context.frame.technique.graph.AddPass<Data>(
        "Deferred view attachments",
        [attributes = m_impl->attributes,
         size = camera.resolution](RenderGraph::Builder& builder, Data& data)
        {
          for (auto attribute : attributes)
          {
            const auto& info = GBufferAttributes[std::size_t(attribute)];
            data.attributes[attribute] =
                builder.Write(builder.Create<RenderGraphImage>(
                    std::string("Deferred ") + info.name,
                    {.width = size.x,
                     .height = size.y,
                     .format = info.format,
                     .usage = {ImageUsage::Sampled,
                               ImageUsage::ColorAttachment}}));
          }
          data.depth = builder.Write(builder.Create<RenderGraphImage>(
              "Deferred depth",
              {.width = size.x,
               .height = size.y,
               .format = ImageFormat::Depth32,
               .usage = {ImageUsage::Sampled,
                         ImageUsage::DepthStencilAttachment}}));
          data.color = builder.Write(builder.Create<RenderGraphImage>(
              "Deferred HDR color",
              {.width = size.x,
               .height = size.y,
               .format = ImageFormat::RGBA16f,
               .usage = {ImageUsage::Sampled, ImageUsage::ColorAttachment}}));
        },
        [size = camera.resolution](const Data& data,
                                   FrameGraphPassResources& resources,
                                   RenderGraphExecutePassContext& execute)
        {
          std::vector<ImageViewHandle> views;
          for (auto [_, image] : data.attributes)
            views.push_back(resources.get<RenderGraphImage>(image).viewHandle);
          auto& commands = *execute.commandList;
          commands.BeginRendering(
              {.depthAttachment =
                   resources.get<RenderGraphImage>(data.depth).viewHandle,
               .clearColor = true,
               .clearDepth = true,
               .width = size.x,
               .height = size.y,
               .colorAttachments = views,
               .clearColorAlpha = 0});
          commands.EndRendering();
          commands.BeginRendering(
              {.colorAttachment =
                   resources.get<RenderGraphImage>(data.color).viewHandle,
               .clearColor = true,
               .clearColorValue = {0.015f, 0.02f, 0.03f},
               .width = size.x,
               .height = size.y});
          commands.EndRendering();
        });
    context.gbuffers[view] = {data.attributes, data.depth};
    context.output.color.push_back(
        {camera.id,
         data.color,
         0,
         0,
         {0, 0, camera.resolution.x, camera.resolution.y},
         ImageFormat::RGBA16f,
         RenderColorEncoding::SceneLinear});
  }
  // Every produced attachment is published with its own semantic and graph
  // version.
  for (auto semantic :
       {DeferredSemantics::Albedo, DeferredSemantics::Normal,
        DeferredSemantics::WorldPosition, DeferredSemantics::SurfaceProperties,
        DeferredSemantics::Emission, DeferredSemantics::Depth,
        DeferredSemantics::Motion, DeferredSemantics::Metallic,
        DeferredSemantics::Roughness})
  {
    if (semantic != DeferredSemantics::Depth &&
        !context.gbuffers.front().attributes.contains(Attribute(semantic)))
      continue;
    auto& outputs = context.output.auxiliary[semantic];
    outputs.clear();
    for (std::size_t view = 0; view < context.gbuffers.size(); ++view)
    {
      auto target = context.output.color[view];
      target.image =
          semantic == DeferredSemantics::Depth
              ? context.gbuffers[view].depth
              : context.gbuffers[view].attributes.at(Attribute(semantic));
      target.format =
          semantic == DeferredSemantics::Depth
              ? ImageFormat::Depth32
              : GBufferAttributes[std::size_t(Attribute(semantic))].format;
      outputs.push_back(target);
    }
  }
}
void DeferredRenderServices::Shade(
    DeferredRenderContext& context, std::string lightingShader,
    std::function<std::vector<LightData>()> lights, Vec3 ambient)
{
  BeginGeometry(context);
  auto state = std::make_shared<ShadeState>(m_impl->device);
  DescriptorSetLayoutDesc images;
  for (unsigned binding : {0u, 1u, 2u, 3u, 4u, 8u})
    images.bindings.push_back({.binding = binding,
                               .type = DescriptorType::CombinedImageSampler,
                               .count = 1,
                               .stages = ShaderStageFlags::Fragment});
  state->images = state->device.CreateDescriptorSetLayout(images);
  state->lights = state->device.CreateDescriptorSetLayout(
      {.bindings = {{.binding = 0,
                     .type = DescriptorType::StorageBuffer,
                     .count = 1,
                     .stages = ShaderStageFlags::Fragment}}});
  state->sampler = state->device.CreateSampler(
      {.addressU = SamplerAddressMode::ClampToEdge,
       .addressV = SamplerAddressMode::ClampToEdge});
  auto pipeline = std::make_shared<Pipeline>(state->device);
  state->pipeline = pipeline;
  std::vector<DescriptorSetLayoutHandle> sets{state->images, state->lights};
  if (context.indirectLayout)
    sets.push_back(context.indirectLayout);
  pipeline->layout = state->device.CreatePipelineLayout({.sets = sets});
  pipeline->vertex = state->device.CreateShaderModule(ShaderCompiler::Compile(
      ExtendedEmbedded::ReadText("forward/Resolve.vert.glsl"),
      {.stage = ShaderStage::Vertex}));
  if (lightingShader.empty())
  {
    lightingShader = ExtendedEmbedded::ReadText(
        "materials/passes/Lit.deferred.EvaluateLight.glsl");
    lightingShader += R"glsl(
uint DeferredLightCount(){return sceneLighting.lightCount.x;}
LightSample DeferredLightAt(uint index,vec3 position){return EvaluateLight(sceneLighting.lights[index],position);}
vec3 DeferredAmbientRadiance(){return sceneLighting.ambientRadiance.rgb;}
)glsl";
  }
  const auto indirect = context.indirectShader.empty() ? R"glsl(
vec3 ForwardDiffuseIrradiance(vec3 position,vec3 normal){return vec3(0);}
vec3 ForwardEnvironmentRadiance(vec3 position,vec3 direction,float roughness){return vec3(0);}
vec2 ForwardEnvironmentBRDF(float nv,float roughness){return vec2(0);}
)glsl"
                                                       : context.indirectShader;
  const auto fragment =
      "#version 450\n" + lightingShader +
      ExtendedEmbedded::ReadText("materials/passes/Lit.BRDF.glsl") + indirect +
      ExtendedEmbedded::ReadText("materials/passes/Lit.IBL.glsl") +
      ExtendedEmbedded::ReadText("deferred/Shade.frag.glsl");
  pipeline->fragment = state->device.CreateShaderModule(
      ShaderCompiler::Compile(fragment, {.stage = ShaderStage::Fragment}));
  pipeline->pipeline =
      state->device.CreatePipeline({.vertexShader = pipeline->vertex,
                                    .fragmentShader = pipeline->fragment,
                                    .colorFormats = {ImageFormat::RGBA16f},
                                    .layout = pipeline->layout});
  for (std::size_t view = 0; view < context.gbuffers.size(); ++view)
  {
    struct Data
    {
      RenderGraphResource color, depth;
      std::map<GBufferAttribute, RenderGraphResource> attributes;
      std::vector<RenderGraphResource> dependencies;
    };
    const auto& pass = context.frame.technique.graph.AddPass<Data>(
        "Deferred lighting",
        [gbuffer = context.gbuffers[view],
         color = context.output.color[view].image,
         dependencies = context.lights](RenderGraph::Builder& builder,
                                        Data& data)
        {
          data.color = builder.Write(builder.Read(color));
          data.depth = builder.Read(gbuffer.depth);
          for (auto [attribute, image] : gbuffer.attributes)
            data.attributes[attribute] = builder.Read(image);
          for (auto resource : dependencies)
            data.dependencies.push_back(builder.Read(resource));
        },
        [state, lights, ambient, family = context.frame.family, view,
         indirectSet = context.indirectSet](
            const Data& data, FrameGraphPassResources& resources,
            RenderGraphExecutePassContext& execute)
        {
          auto bindings = std::make_shared<ShadeBindings>();
          bindings->state = state;
          execute.Defer([bindings] {});
          const auto& camera = family->views[view];
          auto records = lights ? lights() : std::vector<LightData>{};
          LightingFrameData header;
          header.cameraPosition = Vec4(camera.cameraPosition, 1);
          header.ambientRadiance = Vec4(ambient, 0);
          header.lightCount[0] = records.size();
          header.viewProjection = camera.ViewProjection();
          std::vector<std::byte> bytes(sizeof(header) +
                                       records.size() * sizeof(LightData));
          std::memcpy(bytes.data(), &header, sizeof(header));
          if (!records.empty())
            std::memcpy(bytes.data() + sizeof(header), records.data(),
                        records.size() * sizeof(LightData));
          bindings->buffer = state->device.CreateBuffer(
              {.size = bytes.size(),
               .usage = BufferUsage::Storage,
               .initialData = bytes,
               .memory = {.requiredAccess =
                              MemoryAccess::CPUWrite | MemoryAccess::GPURead}});
          bindings->lights =
              state->device.CreateDescriptorSet({.layout = state->lights});
          const std::array writes{DescriptorWrite{
              .binding = 0,
              .resource =
                  DescriptorBufferInfo{bindings->buffer, 0, bytes.size()}}};
          state->device.UpdateDescriptorSet(bindings->lights, writes);
          bindings->images =
              state->device.CreateDescriptorSet({.layout = state->images});
          std::vector<DescriptorWrite> imageWrites;
          for (unsigned i = 0; i < 5; ++i)
            imageWrites.push_back(
                {.binding = i,
                 .resource = DescriptorImageInfo{
                     resources
                         .get<RenderGraphImage>(
                             data.attributes.at(GBufferAttribute(i)))
                         .viewHandle,
                     state->sampler}});
          imageWrites.push_back(
              {.binding = 8,
               .resource = DescriptorImageInfo{
                   resources.get<RenderGraphImage>(data.depth).viewHandle,
                   state->sampler}});
          state->device.UpdateDescriptorSet(bindings->images, imageWrites);
          auto& commands = *execute.commandList;
          commands.MemoryBarrier(
              {.srcStageMask = {PipelineStage::ColorAttachmentOutput,
                                PipelineStage::LateFragmentTests,
                                PipelineStage::EarlyFragmentTests},
               .srcAccessMask = {PipelineAccess::ColorAttachmentWrite,
                                 PipelineAccess::DepthStencilAttachmentWrite},
               .dstStageMask = {PipelineStage::FragmentShader,
                                PipelineStage::ColorAttachmentOutput},
               .dstAccessMask = {PipelineAccess::ShaderSampledRead,
                                 PipelineAccess::ColorAttachmentWrite}});
          commands.BeginRendering(
              {.colorAttachment =
                   resources.get<RenderGraphImage>(data.color).viewHandle,
               .width = camera.resolution.x,
               .height = camera.resolution.y});
          commands.BindPipeline(state->pipeline->pipeline);
          commands.BindDescriptorSet(0, bindings->images);
          commands.BindDescriptorSet(1, bindings->lights);
          if (indirectSet)
            commands.BindDescriptorSet(2, indirectSet);
          commands.Draw(PrimitiveTopology::TriangleList, 3, 0);
          commands.EndRendering();
        });
    context.output.color[view].image = pass.color;
  }
}
DefaultDeferredLightingProvider::DefaultDeferredLightingProvider(
    std::shared_ptr<SceneRenderStore> scene, Vec3 ambient)
    : m_sceneLights(std::move(scene), ambient), m_ambient(ambient)
{
}
void DefaultDeferredLightingProvider::Update(RenderUpdateContext& context)
{
  m_sceneLights.Update(context);
}
DeferredDependencies
DefaultDeferredLightingProvider::GetDeferredDependencies() const
{
  return {{DeferredSemantics::Depth, DeferredSemantics::Albedo,
           DeferredSemantics::Normal, DeferredSemantics::WorldPosition,
           DeferredSemantics::SurfaceProperties, DeferredSemantics::Emission},
          {}};
}
void DefaultDeferredLightingProvider::BuildGraph(DeferredRenderContext& context)
{
  if (!context.rendering)
    throw std::logic_error(
        "Deferred feature did not supply rendering services");
  context.rendering->Shade(
      context, {}, [this] { return m_sceneLights.GetLights(); }, m_ambient);
}
} // namespace Lighthouse
