#include <Lighthouse/Extended/Rendering/DeferredRendering.hpp>
#include <Lighthouse/Extended/Rendering/SceneRenderProviders.hpp>
#include <array>
namespace Lighthouse
{
struct SceneDeferredRenderProvider::History
{
  struct View
  {
    Mat4 viewProjection{1};
    UVec2 resolution{};
    std::map<entt::entity, Mat4> models;
  };
  std::map<RenderViewID, View> views;
};
SceneDeferredRenderProvider::SceneDeferredRenderProvider(
    std::shared_ptr<SceneRenderStore> scene)
    : m_history(std::make_shared<History>()), m_scene(std::move(scene))
{
  if (!m_scene)
    throw std::invalid_argument("Null scene render data");
}
namespace
{
struct FrameBindings
{
  IRenderDevice& device;
  BufferHandle buffer{};
  DescriptorSetHandle set{};
  explicit FrameBindings(IRenderDevice& d) : device(d) {}
  ~FrameBindings()
  {
    if (set)
      device.DestroyDescriptorSet(set);
    if (buffer)
      device.DestroyBuffer(buffer);
  }
};
} // namespace
void SceneDeferredRenderProvider::BuildGraph(DeferredRenderContext& context)
{
  if (!context.rendering)
    throw std::logic_error(
        "Deferred feature did not supply rendering services");
  context.rendering->BeginGeometry(context);
  for (auto it = m_history->views.begin(); it != m_history->views.end();)
  {
    const bool present = std::any_of(
        context.frame.family->views.begin(), context.frame.family->views.end(),
        [&](const auto& view) { return view.id == it->first; });
    if (!present)
      it = m_history->views.erase(it);
    else
      ++it;
  }
  auto& device = context.frame.technique.device;
  struct Draw
  {
    entt::entity entity;
    std::shared_ptr<SceneMeshGPU> mesh;
    std::shared_ptr<SceneMaterialGPU> source;
    std::shared_ptr<const MaterialInstance> material;
    GraphicsPipelineHandle pipeline;
    std::vector<RenderGraphResource> dependencies;
  };
  std::vector<Draw> draws;
  auto importBuffer = [&](const char* name, BufferHandle buffer)
  {
    const auto description = device.GetBufferDesc(buffer);
    return context.frame.technique.graph.Import(
        name,
        ImportedRenderGraphBuffer::Desc{description->size, description->usage},
        ImportedRenderGraphBuffer{buffer});
  };
  for (const auto& instance : m_scene->GetInstances())
  {
    Draw draw;
    draw.entity = instance.entity;
    draw.mesh = m_scene->GetMesh(device, instance.mesh);
    draw.source = m_scene->GetMaterial(device, instance.material);
    draw.material = context.rendering->GetMaterial(draw.source);
    draw.pipeline =
        context.rendering->GetPipeline(draw.mesh->layout, draw.source);
    // Alpha-blended surfaces need a forward transparency stage; never silently
    // turn them into opaque G-buffer surfaces.
    if (draw.source->data.alphaMode == MaterialAsset::AlphaMode::Blend)
      throw std::logic_error(
          "Deferred alpha-blended materials require a transparency compositor");
    draw.dependencies = {
        importBuffer("Deferred mesh vertices", draw.mesh->vertices),
        importBuffer("Deferred mesh indices", draw.mesh->indices),
        importBuffer("Deferred material parameters",
                     draw.material->parametersBuffer)};
    for (const auto& [_, texture] : draw.source->resolvedTextures)
    {
      auto image = texture->sourceImage;
      draw.dependencies.push_back(context.frame.technique.graph.Import(
          "Deferred material texture",
          ImportedRenderGraphImage::Desc{image->resolution.x,
                                         image->resolution.y, image->format,
                                         ImageUsage::Sampled},
          ImportedRenderGraphImage{
              texture->view,
              texture->image,
              {ImageAspect::Color, 0, image->mipLevels, 0, 1}}));
    }
    draws.push_back(std::move(draw));
  }
  for (std::size_t view = 0; view < context.gbuffers.size(); ++view)
  {
    struct Data
    {
      std::map<GBufferAttribute, RenderGraphResource> attributes;
      RenderGraphResource depth;
      std::vector<Draw> draws;
    };
    const auto& pass = context.frame.technique.graph.AddPass<Data>(
        "Deferred scene G-buffer",
        [gbuffer = context.gbuffers[view], draws](RenderGraph::Builder& builder,
                                                  Data& data)
        {
          for (auto [attribute, image] : gbuffer.attributes)
            data.attributes[attribute] = builder.Write(builder.Read(image));
          data.depth = builder.Write(builder.Read(gbuffer.depth));
          data.draws = draws;
          for (auto& draw : data.draws)
            for (auto& resource : draw.dependencies)
              resource = builder.Read(resource);
        },
        [store = m_scene, services = context.rendering, history = m_history,
         family = context.frame.family,
         view](const Data& data, FrameGraphPassResources& resources,
               RenderGraphExecutePassContext& execute)
        {
          const auto& camera = family->views[view];
          const auto previous = history->views.find(camera.id);
          const bool valid = previous != history->views.end() &&
                             previous->second.resolution == camera.resolution;
          History::View next;
          next.viewProjection = camera.ViewProjection();
          next.resolution = camera.resolution;
          std::vector<ImageViewHandle> attachments;
          for (auto [_, image] : data.attributes)
            attachments.push_back(
                resources.get<RenderGraphImage>(image).viewHandle);
          auto& commands = *execute.commandList;
          commands.MemoryBarrier(
              {.srcStageMask = {PipelineStage::ColorAttachmentOutput,
                                PipelineStage::EarlyFragmentTests,
                                PipelineStage::LateFragmentTests},
               .srcAccessMask = {PipelineAccess::ColorAttachmentWrite,
                                 PipelineAccess::DepthStencilAttachmentWrite},
               .dstStageMask = {PipelineStage::ColorAttachmentOutput,
                                PipelineStage::EarlyFragmentTests,
                                PipelineStage::LateFragmentTests},
               .dstAccessMask = {PipelineAccess::ColorAttachmentRead,
                                 PipelineAccess::ColorAttachmentWrite,
                                 PipelineAccess::DepthStencilAttachmentRead,
                                 PipelineAccess::DepthStencilAttachmentWrite}});
          commands.BeginRendering(
              {.depthAttachment =
                   resources.get<RenderGraphImage>(data.depth).viewHandle,
               .width = camera.resolution.x,
               .height = camera.resolution.y,
               .colorAttachments = attachments});
          for (const auto& draw : data.draws)
          {
            DeferredFrameData frame;
            frame.viewProjection = camera.ViewProjection();
            frame.model = store->GetWorldMatrix(draw.entity);
            frame.previousViewProjection = frame.viewProjection;
            frame.previousModel = frame.model;
            if (valid)
            {
              if (const auto model = previous->second.models.find(draw.entity);
                  model != previous->second.models.end())
              {
                frame.previousViewProjection = previous->second.viewProjection;
                frame.previousModel = model->second;
              }
            }
            next.models.emplace(draw.entity, frame.model);
            auto bindings = std::make_shared<FrameBindings>(*draw.mesh->device);
            bindings->buffer = bindings->device.CreateBuffer(
                {.size = sizeof(frame),
                 .usage = BufferUsage::Uniform,
                 .initialData = std::as_bytes(std::span(&frame, 1)),
                 .memory = {.requiredAccess = MemoryAccess::CPUWrite |
                                              MemoryAccess::GPURead}});
            bindings->set = bindings->device.CreateDescriptorSet(
                {.layout = draw.material->material->frameLayout});
            const std::array writes{DescriptorWrite{
                .binding = 0,
                .resource =
                    DescriptorBufferInfo{bindings->buffer, 0, sizeof(frame)}}};
            bindings->device.UpdateDescriptorSet(bindings->set, writes);
            execute.Defer([bindings] {});
            commands.BindPipeline(draw.pipeline);
            commands.BindDescriptorSet(0, bindings->set);
            commands.BindDescriptorSet(1, draw.material->parametersSet);
            commands.BindVertexBuffer(draw.mesh->vertices,
                                      sizeof(Mesh3DVertex));
            commands.BindIndexBuffer(draw.mesh->indices);
            commands.DrawIndexed(draw.mesh->indexCount);
          }
          commands.EndRendering();
          execute.afterSubmit =
              [history, id = camera.id, next = std::move(next)]() mutable
          { history->views.insert_or_assign(id, std::move(next)); };
        });
    const auto previous = context.gbuffers[view];
    context.gbuffers[view] = {pass.attributes, pass.depth};
    for (auto& [_, outputs] : context.output.auxiliary)
    {
      auto& target = outputs[view];
      if (target.image == previous.depth)
        target.image = pass.depth;
      else
        for (auto [attribute, image] : previous.attributes)
          if (target.image == image)
          {
            target.image = pass.attributes.at(attribute);
            break;
          }
    }
  }
}
} // namespace Lighthouse
