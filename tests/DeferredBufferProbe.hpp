#pragma once
#include <Lighthouse/Engine/Assets/ShaderCompiler.hpp>
#include <Lighthouse/Extended/EmbeddedResources.hpp>
#include <Lighthouse/Extended/Rendering/DeferredRendering.hpp>
namespace Lighthouse
{
// GPU diagnostic used by the raster integration test; ordinary rendering is
// untouched. It verifies actual attachment contents, not just their presence.
class DeferredBufferProbe final : public IDeferredPostProcess
{
public:
  bool requested{true};
  DeferredBufferProbe(IRenderDevice& device, ImageHandle image,
                      ImageViewHandle view)
      : m_device(device), m_image(image), m_view(view)
  {
  }
  std::uint64_t GetConfigurationRevision() const noexcept override
  {
    return requested;
  }
  DeferredDependencies GetDeferredDependencies() const override
  {
    return requested ? DeferredDependencies{{DeferredSemantics::Motion,
                                             DeferredSemantics::Metallic,
                                             DeferredSemantics::Roughness},
                                            {}}
                     : DeferredDependencies{};
  }
  void BuildGraphDeferred(DeferredRenderContext& context) override
  {
    if (!requested)
      return;
    struct State
    {
      IRenderDevice& device;
      DescriptorSetLayoutHandle setLayout{};
      PipelineLayoutHandle layout{};
      ShaderModuleHandle vertex{}, fragment{};
      GraphicsPipelineHandle pipeline{};
      SamplerHandle sampler{};
      explicit State(IRenderDevice& d) : device(d) {}
      ~State()
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
    auto state = std::make_shared<State>(m_device);
    DescriptorSetLayoutDesc layout;
    for (unsigned i = 0; i < 3; ++i)
      layout.bindings.push_back({.binding = i,
                                 .type = DescriptorType::CombinedImageSampler,
                                 .count = 1,
                                 .stages = ShaderStageFlags::Fragment});
    state->setLayout = m_device.CreateDescriptorSetLayout(layout);
    state->layout = m_device.CreatePipelineLayout({.sets = {state->setLayout}});
    state->sampler =
        m_device.CreateSampler({.minification = SamplerFilter::Nearest,
                                .magnification = SamplerFilter::Nearest});
    state->vertex = m_device.CreateShaderModule(ShaderCompiler::Compile(
        ExtendedEmbedded::ReadText("forward/Resolve.vert.glsl"),
        {.stage = ShaderStage::Vertex}));
    state->fragment = m_device.CreateShaderModule(
        ShaderCompiler::Compile(R"glsl(
#version 450
layout(set=0,binding=0) uniform sampler2D motion;
layout(set=0,binding=1) uniform sampler2D metallic;
layout(set=0,binding=2) uniform sampler2D roughness;
layout(location=0) out vec4 color;
void main(){ivec2 p=ivec2(gl_FragCoord.xy);color=vec4(length(texelFetch(motion,p,0).xy)*20.0,texelFetch(metallic,p,0).r,texelFetch(roughness,p,0).r,1);}
)glsl",
                                {.stage = ShaderStage::Fragment}));
    state->pipeline =
        m_device.CreatePipeline({.vertexShader = state->vertex,
                                 .fragmentShader = state->fragment,
                                 .colorFormats = {ImageFormat::RGBA8},
                                 .layout = state->layout});
    const auto diagnostic = context.frame.technique.graph.Import(
        "Deferred probe output",
        ImportedRenderGraphImage::Desc{128, 128, ImageFormat::RGBA8,
                                       ImageUsage::ColorAttachment |
                                           ImageUsage::TransferSource},
        ImportedRenderGraphImage{m_view, m_image});
    const std::array inputs{
        context.output.auxiliary.at(DeferredSemantics::Motion)[0].image,
        context.output.auxiliary.at(DeferredSemantics::Metallic)[0].image,
        context.output.auxiliary.at(DeferredSemantics::Roughness)[0].image};
    struct Data
    {
      std::array<RenderGraphResource, 3> inputs;
      RenderGraphResource diagnostic;
    };
    context.frame.technique.graph.AddPass<Data>(
        "Deferred attachment probe",
        [inputs, diagnostic](RenderGraph::Builder& builder, Data& data)
        {
          for (unsigned i = 0; i < 3; ++i)
            data.inputs[i] = builder.Read(inputs[i]);
          data.diagnostic = builder.Write(diagnostic);
          builder.SetSideEffect();
        },
        [state](const Data& data, FrameGraphPassResources& resources,
                RenderGraphExecutePassContext& execute)
        {
          const auto set =
              state->device.CreateDescriptorSet({.layout = state->setLayout});
          execute.Defer([state, set]
                        { state->device.DestroyDescriptorSet(set); });
          std::array<DescriptorWrite, 3> writes;
          for (unsigned i = 0; i < 3; ++i)
            writes[i] = {
                .binding = i,
                .resource = DescriptorImageInfo{
                    resources.get<RenderGraphImage>(data.inputs[i]).viewHandle,
                    state->sampler}};
          state->device.UpdateDescriptorSet(set, writes);
          auto& commands = *execute.commandList;
          commands.MemoryBarrier(
              {.srcStageMask = PipelineStage::ColorAttachmentOutput,
               .srcAccessMask = PipelineAccess::ColorAttachmentWrite,
               .dstStageMask = PipelineStage::FragmentShader,
               .dstAccessMask = PipelineAccess::ShaderSampledRead});
          commands.BeginRendering(
              {.colorAttachment =
                   resources.get<ImportedRenderGraphImage>(data.diagnostic)
                       .viewHandle,
               .width = 128,
               .height = 128});
          commands.BindPipeline(state->pipeline);
          commands.BindDescriptorSet(0, set);
          commands.Draw(PrimitiveTopology::TriangleList, 3, 0);
          commands.EndRendering();
        });
  }

private:
  IRenderDevice& m_device;
  ImageHandle m_image;
  ImageViewHandle m_view;
};
} // namespace Lighthouse
