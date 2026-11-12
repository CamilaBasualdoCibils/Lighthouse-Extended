#include <Lighthouse/Backends/Render/OpenGL/OpenGLBackend.hpp>
#include <Lighthouse/Backends/Render/Vulkan/VulkanBackend.hpp>
#include <Lighthouse/Engine/Assets/ShaderCompiler.hpp>
#include <Lighthouse/Extended/EmbeddedResources.hpp>
#include <Lighthouse/Extended/Rendering/Bloom.hpp>
#include <Lighthouse/Extended/Rendering/ForwardRendering.hpp>
#include <Lighthouse/Extended/Rendering/Renderer.hpp>
#include <array>
#include <cmath>
#include <iostream>
using namespace Lighthouse;
namespace
{
void Check(bool value, const char* message)
{
  if (!value)
    throw std::runtime_error(message);
}
struct SourceGPU
{
  IRenderDevice& device;
  DescriptorSetLayoutHandle setLayout{};
  PipelineLayoutHandle layout{};
  ShaderModuleHandle vertex{}, fragment{};
  GraphicsPipelineHandle pipeline{};
  explicit SourceGPU(IRenderDevice& device) : device(device) {}
  ~SourceGPU()
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
  }
};
struct SourceBindings
{
  std::shared_ptr<SourceGPU> gpu;
  BufferHandle buffer{};
  DescriptorSetHandle set{};
  ~SourceBindings()
  {
    if (set)
      gpu->device.DestroyDescriptorSet(set);
    if (buffer)
      gpu->device.DestroyBuffer(buffer);
  }
};
class ImpulseFeature final : public IRenderFeature
{
public:
  // GPU-generated tent reconstruction of a moving subpixel highlight. Its
  // source energy stays constant as the highlight crosses pixel boundaries.
  Vec4 stimulus{64.5f, 64.5f, 10000, 0};
  Bloom bloom{BloomSettings{.Intensity = 1, .MipCount = 6}};
  RenderFeatureResult BuildGraph(RenderFeatureContext& frame) override
  {
    auto gpu = std::make_shared<SourceGPU>(frame.technique.device);
    gpu->setLayout = gpu->device.CreateDescriptorSetLayout(
        {.bindings = {{.binding = 0,
                       .type = DescriptorType::UniformBuffer,
                       .count = 1,
                       .stages = ShaderStageFlags::Fragment}}});
    gpu->layout = gpu->device.CreatePipelineLayout({.sets = {gpu->setLayout}});
    gpu->vertex = gpu->device.CreateShaderModule(ShaderCompiler::Compile(
        ExtendedEmbedded::ReadText("forward/Resolve.vert.glsl"),
        {.stage = ShaderStage::Vertex}));
    gpu->fragment = gpu->device.CreateShaderModule(
        ShaderCompiler::Compile(R"glsl(
#version 450
layout(set=0,binding=0,std140) uniform Stimulus { vec4 point; };
layout(location=0) out vec4 color;
void main() {
  vec2 distance=abs(gl_FragCoord.xy-point.xy);
  vec2 tent=max(vec2(1)-distance,vec2(0));
  float value=point.w>0 ? float(all(lessThan(distance,vec2(point.w)))) : tent.x*tent.y;
  color=vec4(vec3(value*point.z),1);
}
)glsl",
                                {.stage = ShaderStage::Fragment}));
    gpu->pipeline =
        gpu->device.CreatePipeline({.vertexShader = gpu->vertex,
                                    .fragmentShader = gpu->fragment,
                                    .colorFormats = {ImageFormat::RGBA16f},
                                    .layout = gpu->layout});
    struct Data
    {
      RenderGraphResource image;
    };
    const auto& pass = frame.technique.graph.AddPass<Data>(
        "Bloom stability input",
        [](RenderGraph::Builder& builder, Data& data)
        {
          data.image = builder.Write(builder.Create<RenderGraphImage>(
              "Sparse HDR highlight",
              {.width = 128,
               .height = 128,
               .format = ImageFormat::RGBA16f,
               .usage = {ImageUsage::Sampled, ImageUsage::ColorAttachment}}));
        },
        [this, gpu](const Data& data, FrameGraphPassResources& resources,
                    RenderGraphExecutePassContext& execute)
        {
          auto bindings = std::make_shared<SourceBindings>();
          bindings->gpu = gpu;
          bindings->buffer = gpu->device.CreateBuffer(
              {.size = sizeof(stimulus),
               .usage = BufferUsage::Uniform,
               .initialData = std::as_bytes(std::span(&stimulus, 1)),
               .memory = {.requiredAccess =
                              MemoryAccess::CPUWrite | MemoryAccess::GPURead}});
          bindings->set =
              gpu->device.CreateDescriptorSet({.layout = gpu->setLayout});
          const std::array writes{DescriptorWrite{
              .binding = 0,
              .resource =
                  DescriptorBufferInfo{bindings->buffer, 0, sizeof(stimulus)}}};
          gpu->device.UpdateDescriptorSet(bindings->set, writes);
          execute.Defer([bindings] {});
          auto& commands = *execute.commandList;
          commands.BeginRendering(
              {.colorAttachment =
                   resources.get<RenderGraphImage>(data.image).viewHandle,
               .width = 128,
               .height = 128});
          commands.BindPipeline(gpu->pipeline);
          commands.BindDescriptorSet(0, bindings->set);
          commands.Draw(PrimitiveTopology::TriangleList, 3, 0);
          commands.EndRendering();
        });
    ForwardRenderContext context{
        frame, ForwardRenderResult{RenderFeatureResult{
                   {RenderOutputTarget{frame.family->views[0].id,
                                       pass.image,
                                       0,
                                       0,
                                       {0, 0, 128, 128},
                                       ImageFormat::RGBA16f,
                                       RenderColorEncoding::SceneLinear}}}}};
    bloom.BuildGraphForward(context);
    auto resolve =
        std::make_shared<ForwardRenderServices>(frame.technique.device, "");
    frame.keepAlive.push_back(resolve);
    resolve->Resolve(frame, context.output);
    return std::move(context.output);
  }
};
struct Metrics
{
  unsigned maximum{};
  std::uint64_t energy{};
};
Metrics Halo(std::span<const std::byte> pixels, Vec4 point)
{
  Metrics result;
  for (unsigned y = 0; y < 128; ++y)
    for (unsigned x = 0; x < 128; ++x)
    {
      const float exclusion = point.w > 0 ? point.w + 4 : 4;
      if (std::abs(float(x) + .5f - point.x) <= exclusion &&
          std::abs(float(y) + .5f - point.y) <= exclusion)
        continue;
      const auto value = std::to_integer<unsigned>(pixels[(y * 128 + x) * 4]);
      result.maximum = std::max(result.maximum, value);
      result.energy += value;
    }
  return result;
}
} // namespace
int main(int argc, char** argv)
{
  try
  {
    const bool vulkan = argc > 1 && std::string_view(argv[1]) == "--vulkan";
    std::unique_ptr<IGraphicsBackend> backend;
    if (vulkan)
      backend = std::make_unique<VulkanBackend>();
    else
      backend = std::make_unique<OpenGLBackend>();
    std::string diagnostic;
    Check(backend->Initialize(diagnostic), diagnostic.c_str());
    {
      auto device = backend->CreateDevice(
          {.queues = {{.capabilities = RenderQueueCapability::Graphics |
                                       RenderQueueCapability::Transfer,
                       .minCount = 1}}});
      RenderGraphComposer composer(std::move(device));
      auto& gpu = *composer.GetRenderDevice();
      const auto image =
          gpu.CreateImage({.width = 128,
                           .height = 128,
                           .format = ImageFormat::RGBA8,
                           .usage = {ImageUsage::TransferDestination,
                                     ImageUsage::TransferSource}});
      const auto imageView =
          gpu.CreateImageView({.image = image, .format = ImageFormat::RGBA8});
      {
        const auto ref = composer.AddTechnique<Renderer>(&composer);
        auto& renderer = *composer.GetTechnique<Renderer>(ref);
        auto& view = renderer.CreateView(std::make_shared<GraphRenderOutput>(
            [image, imageView](TechniqueBuildContext& context,
                               const RenderViewFamily& family)
            {
              const auto output = context.graph.Import(
                  "Bloom stability output",
                  ImportedRenderGraphImage::Desc{
                      128, 128, ImageFormat::RGBA8,
                      ImageUsage::TransferDestination |
                          ImageUsage::TransferSource},
                  ImportedRenderGraphImage{imageView,image,{},context.device.GetImageLayout(image)});
              return RenderOutputTargets{{family.views[0].id,
                                          output,
                                          0,
                                          0,
                                          {0, 0, 128, 128},
                                          ImageFormat::RGBA8,
                                          RenderColorEncoding::SRGB,
                                          RenderTargetKind::ImportedImage}};
            }));
        RenderViewData camera;
        camera.id = 1;
        camera.resolution = {128, 128};
        view.SetViewFamily({{camera}});
        auto& feature = view.SetRenderFeature<ImpulseFeature>();
        renderer.Update(0);
        composer.Compile();
        auto render = [&]
        {
          composer.Render();
          gpu.WaitIdle();
          return gpu.ReadImage(image);
        };
        std::uint64_t minEnergy = UINT64_MAX, maxEnergy = 0;
        unsigned maxHalo = 0;
        for (unsigned step = 0; step < 9; ++step)
        {
          feature.stimulus.x = 64.5f + float(step) * .125f;
          auto pixels = render();
          auto metrics = Halo(pixels, feature.stimulus);
          Check(pixels == render(),
                "Stationary bloom flickers across identical frames");
          minEnergy = std::min(minEnergy, metrics.energy);
          maxEnergy = std::max(maxEnergy, metrics.energy);
          maxHalo = std::max(maxHalo, metrics.maximum);
          std::cout << "phase " << step << ": halo peak " << metrics.maximum
                    << ", energy " << metrics.energy << '\n';
        }
        std::cout << "halo range " << minEnergy << ".." << maxEnergy
                  << ", peak " << maxHalo << std::endl;
        Check(maxHalo <= 64,
              "An isolated HDR pixel produces an excessive bloom halo");
        Check(
            maxEnergy - minEnergy <=
                std::max<std::uint64_t>(256, maxEnergy / 3),
            "Subpixel highlight motion causes excessive bloom energy flicker");
        auto settings = feature.bloom.GetSettings();
        settings.Threshold = 2;
        feature.bloom.SetSettings(settings);
        Check(Halo(render(), feature.stimulus).energy <= maxEnergy,
              "Increasing the threshold amplifies an isolated highlight");
        settings.Threshold = 1;
        feature.bloom.SetSettings(settings);
        feature.stimulus.z = 0;
        const auto dark = render();
        for (std::size_t i = 0; i < dark.size(); i += 4)
          Check(dark[i] == std::byte{0} && dark[i + 1] == std::byte{0} &&
                    dark[i + 2] == std::byte{0},
                "Bloom retains stale highlights after the input becomes dark");
        feature.stimulus = {64.5f, 64.5f, 20, 16};
        const auto broad = Halo(render(), feature.stimulus);
        std::cout << "broad highlight halo peak " << broad.maximum
                  << ", energy " << broad.energy << std::endl;
        Check(broad.maximum > 128,
              "Bloom suppression incorrectly removes broad bright surfaces");
        // Soft knee affects the threshold neighborhood, not already-bright HDR
        // regions. Exercise live shader settings without rebuilding the graph.
        feature.stimulus = {64.5f, 64.5f, 0.9f, 128};
        settings.SoftKnee = 0;
        feature.bloom.SetSettings(settings);
        const auto hardKnee = render();
        settings.SoftKnee = 1;
        feature.bloom.SetSettings(settings);
        const auto softKnee = render();
        const auto center = (64 * 128 + 64) * 4;
        const auto hardValue = std::to_integer<int>(hardKnee[center]);
        const auto softValue = std::to_integer<int>(softKnee[center]);
        std::cout << "near-threshold soft knee: " << hardValue << " -> "
                  << softValue << std::endl;
        Check(softValue > hardValue + 5,
              "Soft knee does not reach the GPU or soften the threshold");
        feature.stimulus.z = 20;
        const auto brightSoft = render();
        settings.SoftKnee = 0;
        feature.bloom.SetSettings(settings);
        Check(
            brightSoft == render(),
            "Soft knee should not change highlights well above the threshold");
        composer.GetRenderGraph().Clear();
        composer.RemoveTechnique(ref.handle);
      }
      gpu.DestroyImageView(imageView);
      gpu.DestroyImage(image);
      std::cout << backend->Name()
                << ": sparse-highlight stability and broad highlights passed\n";
    }
    backend->Shutdown();
    return 0;
  }
  catch (const std::exception& error)
  {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
