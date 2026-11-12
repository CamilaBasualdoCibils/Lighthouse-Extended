#include <Lighthouse/Extended/Rendering/ForwardRendering.hpp>
#include <Lighthouse/Extended/Rendering/SceneProvider.hpp>

#include <algorithm>

namespace Lighthouse
{
void SceneForwardRenderProvider::BuildGraph(ForwardRenderContext& ctx)
{
  if (!ctx.rendering)
    throw std::logic_error("Forward feature did not supply rendering services");
  ctx.rendering->BeginGeometry(ctx);
  auto& device = ctx.frame.technique.device;
  const auto instances = m_scene->GetInstances();
  const auto importBuffer = [&](const char* name, BufferHandle handle)
  {
    const auto description = device.GetBufferDesc(handle);
    return ctx.frame.technique.graph.Import(
        name,
        ImportedRenderGraphBuffer::Desc{description->size, description->usage},
        ImportedRenderGraphBuffer{handle});
  };
  struct Draw
  {
    entt::entity entity;
    std::shared_ptr<SceneMeshGPU> mesh;
    std::shared_ptr<SceneMaterialGPU> source;
    std::shared_ptr<const MaterialInstance> material;
    std::uint32_t index{};
    GraphicsPipelineHandle pipeline;
    std::vector<RenderGraphResource> resources;
  };
  for (std::size_t view = 0; view < ctx.frame.family->views.size(); ++view)
  {
    std::vector<Draw> draws;
    for (const auto& instance : instances)
    {
      Draw draw;
      draw.entity = instance.entity;
      draw.mesh = m_scene->GetMesh(device, instance.mesh);
      const auto material = m_scene->GetMaterial(device, instance.material);
      draw.source = material;
      draw.material = ctx.rendering->GetMaterial(material);
      draw.index = draws.size();
      draw.resources = {
          importBuffer("Forward mesh vertices", draw.mesh->vertices),
          importBuffer("Forward mesh indices", draw.mesh->indices)};
      for (const auto& [_, texture] : material->resolvedTextures)
      {
        const auto size = texture->sourceImage->resolution;
        draw.resources.push_back(ctx.frame.technique.graph.Import(
            "Forward material texture",
            ImportedRenderGraphImage::Desc{size.x, size.y, texture->sourceImage->format,
                                           ImageUsage::Sampled},
            ImportedRenderGraphImage{texture->view, texture->image,
                {ImageAspect::Color, 0, texture->sourceImage->mipLevels, 0, 1},ImageLayout::ShaderReadOnly}));
      }
      draws.push_back(std::move(draw));
    }
    std::vector<std::shared_ptr<const MaterialInstance>> materials;
    for (const auto& draw : draws) materials.push_back(draw.material);
    auto bindings = std::make_shared<SceneBindings>(device, materials);
    for (auto& draw : draws) draw.pipeline=ctx.rendering->GetPipeline(draw.mesh->layout, draw.source, *bindings);
    struct LightingSet {
      IRenderDevice& device;
      DescriptorSetHandle set;
      ~LightingSet() { if(set) device.DestroyDescriptorSet(set); }
    };
    auto lightSet=std::shared_ptr<LightingSet>(new LightingSet{device,ctx.rendering->CreateLightingSet(ctx.lightingBuffer)});
    std::vector<RenderGraphResource> globals;
    for(auto buffer : bindings->Buffers()) globals.push_back(importBuffer("Forward scene table",buffer));
    struct Data
    {
      RenderGraphResource color, depth;
      std::vector<Draw> draws;
      std::vector<RenderGraphResource> lights;
    };
    const auto color = ctx.output.color[view].image;
    const auto depth =
        ctx.output.auxiliary.at(ForwardSemantics::Depth)[view].image;
    const auto& pass = ctx.frame.technique.graph.AddPass<Data>(
        "Forward scene geometry",
        [color, depth, draws = std::move(draws),
         lights = ctx.lights, globals](RenderGraph::Builder& builder, Data& data)
        {
          data.color = builder.Write(builder.Read(color));
          data.depth = builder.Write(builder.Read(depth));
          data.draws = draws;
          for (auto& draw : data.draws)
            for (auto& resource : draw.resources)
              resource = builder.Read(resource);
          for (auto resource : lights) data.lights.push_back(builder.Read(resource));
          for (auto resource : globals) data.lights.push_back(builder.Read(resource));
        },
        [store = m_scene, services = ctx.rendering, family = ctx.frame.family,
         view, bindings, lightSet](const Data& data, FrameGraphPassResources& resources,
               RenderGraphExecutePassContext& execute)
        {
          const auto& camera = family->views[view];
          std::vector<const Draw*> sorted;
          for (const auto& draw : data.draws)
            sorted.push_back(&draw);
          std::stable_sort(
              sorted.begin(), sorted.end(),
              [&](const Draw* a, const Draw* b)
              {
                const bool ablend = a->source->data.alphaMode ==
                                    MaterialAsset::AlphaMode::Blend;
                const bool bblend = b->source->data.alphaMode ==
                                    MaterialAsset::AlphaMode::Blend;
                if (ablend != bblend)
                  return !ablend;
                if (ablend)
                {
                  auto da = Vec3(store->GetWorldMatrix(a->entity)[3]) -
                            camera.cameraPosition;
                  auto db = Vec3(store->GetWorldMatrix(b->entity)[3]) -
                            camera.cameraPosition;
                  return glm::dot(da, da) > glm::dot(db, db);
                }
                return a->pipeline.index < b->pipeline.index;
              });
          std::vector<SceneObjectData> objects(data.draws.size());
          for(const auto& draw : data.draws) {
            auto& object=objects[draw.index];
            object.model=store->GetWorldMatrix(draw.entity);
            object.previousModel=object.model;
            object.material=bindings->MaterialOffsets(draw.index);
          }
          bindings->Update({camera.ViewProjection(),camera.ViewProjection(),Vec4(camera.cameraPosition,1)},objects);
          auto& commands = *execute.commandList;
          // Preserve attachments written by the clear pass or an earlier
          // provider.
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
              {.colorAttachment =
                   resources.get<RenderGraphImage>(data.color).viewHandle,
               .depthAttachment =
                   resources.get<RenderGraphImage>(data.depth).viewHandle,
               .width = camera.resolution.x,
               .height = camera.resolution.y});
          GraphicsPipelineHandle current{};
          bool globalsBound=false;
          for (const auto* draw : sorted)
          {
            const bool changed=current!=draw->pipeline;
            if(changed) { commands.BindPipeline(draw->pipeline); current=draw->pipeline; }
            if(!globalsBound) {
              commands.BindDescriptorSet(0,bindings->FrameSet());
              if(bindings->ShaderOptions().bindless) commands.BindDescriptorSet(1,bindings->AssetSet());
            }
            if(!globalsBound || (changed && !bindings->ShaderOptions().bindless)) {
              if(lightSet->set) commands.BindDescriptorSet(2,lightSet->set);
              if(services->IndirectSet()) commands.BindDescriptorSet(3,services->IndirectSet());
            }
            globalsBound=true;
            if(!bindings->ShaderOptions().bindless) commands.BindDescriptorSet(1,draw->material->parametersSet);
            commands.SetDrawIndex(draw->index);
            commands.BindVertexBuffer(draw->mesh->vertices,sizeof(Mesh3DVertex));
            commands.BindIndexBuffer(draw->mesh->indices);
            commands.DrawIndexed(draw->mesh->indexCount);
          }
          commands.EndRendering();
        });
    ctx.output.color[view].image = pass.color;
    ctx.output.auxiliary[ForwardSemantics::Depth][view].image = pass.depth;
  }
}
} // namespace Lighthouse
