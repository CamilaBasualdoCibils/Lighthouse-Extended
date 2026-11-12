#include "ImageUpload.hpp"
#include <Lighthouse/Extended/Rendering/EnvironmentProviders.hpp>
#include <Lighthouse/Extended/Rendering/ForwardRendering.hpp>
#include <Lighthouse/Extended/EmbeddedResources.hpp>
#include <Lighthouse/Extended/World/HostBuffer.hpp>
#include <Lighthouse/Engine/Assets/ShaderCompiler.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <numbers>

namespace Lighthouse
{
namespace
{
constexpr float pi = std::numbers::pi_v<float>;
Vec3 Pixel(const ImageData& image, int x, int y)
{
  const auto size = image.GetResolution();
  x = (x % int(size.x) + int(size.x)) % int(size.x);
  y = std::clamp(y, 0, int(size.y) - 1);
  std::array<float, 4> pixel;
  std::memcpy(pixel.data(), image.GetData().data() + (std::size_t(y) * size.x + x) * sizeof(pixel), sizeof(pixel));
  return {pixel[0], pixel[1], pixel[2]};
}
void ValidateHDR(const ImageData& image)
{
  image.Validate();
  if (image.GetDepth()!=1 || image.GetFormat() != ImageFormat::RGBA32f)
    throw std::invalid_argument("HDRI requires linear RGBA32f pixels");
  for (std::size_t i = 0; i < image.GetData().size(); i += sizeof(float)) {
    float value; std::memcpy(&value, image.GetData().data() + i, sizeof(value));
    if (!std::isfinite(value) || value < 0)
      throw std::invalid_argument("HDRI requires finite, nonnegative pixels");
  }
}
Vec3 Direction(float u, float v)
{
  const auto longitude = (u - 0.5f) * 2 * pi, latitude = v * pi;
  return {std::cos(longitude) * std::sin(latitude), std::cos(latitude), std::sin(longitude) * std::sin(latitude)};
}
Vec3 Sample(const ImageData& image, Vec3 direction)
{
  const auto size = image.GetResolution();
  const float x = (std::atan2(direction.z, direction.x) / (2 * pi) + 0.5f) * size.x - 0.5f;
  const float y = std::acos(std::clamp(direction.y, -1.0f, 1.0f)) / pi * size.y - 0.5f;
  const int ix = int(std::floor(x)), iy = int(std::floor(y));
  return glm::mix(glm::mix(Pixel(image, ix, iy), Pixel(image, ix+1, iy), x-ix),
                  glm::mix(Pixel(image, ix, iy+1), Pixel(image, ix+1, iy+1), x-ix), y-iy);
}
float RadicalInverse(std::uint32_t i)
{
  i = (i << 16) | (i >> 16);
  i = ((i & 0x55555555u) << 1) | ((i & 0xaaaaaaaau) >> 1);
  i = ((i & 0x33333333u) << 2) | ((i & 0xccccccccu) >> 2);
  i = ((i & 0x0f0f0f0fu) << 4) | ((i & 0xf0f0f0f0u) >> 4);
  i = ((i & 0x00ff00ffu) << 8) | ((i & 0xff00ff00u) >> 8);
  return float(i) * 2.3283064365386963e-10f;
}
RenderGraphResource ImportTexture(RenderFeatureContext& frame, const char* name,
                                  const std::shared_ptr<SceneTextureGPU>& texture)
{
  const auto size = texture->sourceImage->resolution;
  frame.keepAlive.push_back(texture);
  return frame.technique.graph.Import(name,
    ImportedRenderGraphImage::Desc{size.x, size.y, texture->sourceImage->format, ImageUsage::Sampled},
    ImportedRenderGraphImage{texture->view, texture->image,
      {ImageAspect::Color,0,texture->sourceImage->mipLevels,0,1},ImageLayout::ShaderReadOnly});
}
// Per-graph descriptors/pipelines; captured by execution callbacks and keepAlive.
struct Bindings
{
  IRenderDevice* device{};
  DescriptorSetLayoutHandle setLayout{};
  DescriptorSetHandle set{};
  PipelineLayoutHandle layout{};
  ShaderModuleHandle vertex{}, fragment{};
  GraphicsPipelineHandle pipeline{};
  BufferHandle frame{};
  ~Bindings() {
    if (pipeline) device->DestroyPipeline(pipeline);
    if (vertex) device->DestroyModule(vertex);
    if (fragment) device->DestroyModule(fragment);
    if (layout) device->DestroyPipelineLayout(layout);
    if (set) device->DestroyDescriptorSet(set);
    if (setLayout) device->DestroyDescriptorSetLayout(setLayout);
    if (frame) device->DestroyBuffer(frame);
  }
};

// Cached initialization passes build specular convolution mips and the DFG LUT.
std::shared_ptr<SceneTextureGPU> MakeTexture(IRenderDevice& device, UVec2 size,
                                            std::uint32_t levels, ImageFormat format)
{
  auto image=std::make_shared<SceneImageGPU>(); image->device=&device;
  image->resolution=size;image->format=format;image->mipLevels=levels;
  image->image=device.CreateImage({.width=size.x,.height=size.y,.format=format,.mipLevels=levels,
                                   .usage={ImageUsage::Sampled,ImageUsage::ColorAttachment}});
  image->view=device.CreateImageView({.image=image->image,.format=format,.mipLevelCount=levels});
  auto texture=std::make_shared<SceneTextureGPU>();texture->device=&device;texture->sourceImage=image;
  texture->image=image->image;texture->view=image->view;
  texture->sampler=device.CreateSampler({.mipmap=levels>1 ? SamplerMipmapFilter::Linear : SamplerMipmapFilter::None,
    .addressU=SamplerAddressMode::ClampToEdge,.addressV=SamplerAddressMode::ClampToEdge});
  return texture;
}
void FilterTexture(IRenderDevice& device, SceneTextureGPU& target,
                    const std::shared_ptr<SceneTextureGPU>& source, const char* fragment,
                    std::uint32_t samples)
{
  auto state=std::make_shared<Bindings>();state->device=&device;
  std::vector<DescriptorBindingDesc> bindings{
    {.binding=0,.type=DescriptorType::UniformBuffer,.count=1,.stages=ShaderStageFlags::Fragment}};
  if (source) bindings.push_back({.binding=1,.type=DescriptorType::CombinedImageSampler,.count=1,.stages=ShaderStageFlags::Fragment});
  state->setLayout=device.CreateDescriptorSetLayout({.bindings=bindings});
  state->layout=device.CreatePipelineLayout({.sets={state->setLayout}});
  state->vertex=device.CreateShaderModule(ShaderCompiler::Compile(ExtendedEmbedded::ReadText("forward/Resolve.vert.glsl"),{.stage=ShaderStage::Vertex}));
  state->fragment=device.CreateShaderModule(ShaderCompiler::Compile(ExtendedEmbedded::ReadText(fragment),{.stage=ShaderStage::Fragment}));
  state->pipeline=device.CreatePipeline({.vertexShader=state->vertex,.fragmentShader=state->fragment,
    .colorFormats={target.sourceImage->format},.layout=state->layout});
  auto* queue=device.GetQueue(0);
  if (!queue || !queue->GetCapabilities().Contains(RenderQueueCapability::Graphics))
    throw std::runtime_error("IBL filtering requires graphics queue zero");
  auto commands=device.CreateCommandList(0);commands->Begin();
  std::vector<std::shared_ptr<Bindings>> sets;
  struct Views {
    IRenderDevice& device;std::vector<ImageViewHandle> values;
    ~Views() { for (auto view:values) device.DestroyImageView(view); }
  } views{device,{}};
  auto size=target.sourceImage->resolution;
  const auto count=target.sourceImage->mipLevels;
  commands->ImageBarrier({.image=target.image,.oldLayout=ImageLayout::Undefined,.newLayout=ImageLayout::ColorAttachment,
      .subresource={ImageAspect::Color,0,count,0,1}});
  for (unsigned mip=0;mip<count;++mip) {
    auto set=std::make_shared<Bindings>();set->device=&device;
    // Parameters are distinct per draw: descriptors resolve at submission time.
    const Vec4 parameters(float(size.x),float(size.y),float(samples),count>1 ? float(mip)/(count-1) : 0.0f);
    set->frame=device.CreateBuffer({.size=sizeof(parameters),.usage=BufferUsage::Uniform,
                                   .initialData=std::as_bytes(std::span(&parameters,1))});
    set->set=device.CreateDescriptorSet({.layout=state->setLayout});
    std::vector<DescriptorWrite> writes{{.binding=0,.resource=DescriptorBufferInfo{set->frame,0,sizeof(parameters)}}};
    if (source) writes.push_back({.binding=1,.resource=DescriptorImageInfo{source->view,source->sampler}});
    device.UpdateDescriptorSet(set->set,writes);
    const auto view=device.CreateImageView({.image=target.image,.format=target.sourceImage->format,.baseMipLevel=mip});
    views.values.push_back(view);
    commands->BeginRendering({.colorAttachment=view,.width=size.x,.height=size.y});
    commands->BindPipeline(state->pipeline);commands->BindDescriptorSet(0,set->set);
    commands->Draw(PrimitiveTopology::TriangleList,3,0);commands->EndRendering();
    sets.push_back(std::move(set));size={std::max(1u,size.x/2),std::max(1u,size.y/2)};
  }
  commands->ImageBarrier({.image=target.image,.oldLayout=ImageLayout::ColorAttachment,.newLayout=ImageLayout::ShaderReadOnly,
      .subresource={ImageAspect::Color,0,count,0,1}});
  commands->MemoryBarrier({.srcStageMask=PipelineStage::ColorAttachmentOutput,
    .srcAccessMask=PipelineAccess::ColorAttachmentWrite,.dstStageMask=PipelineStage::FragmentShader,
    .dstAccessMask=PipelineAccess::ShaderSampledRead});
  commands->End();
  const auto fence=device.CreateFence();
  try { queue->Submit(*commands,fence);if (!device.WaitForFence(fence)) throw std::runtime_error("IBL filtering wait failed"); }
  catch (...) { device.WaitIdle();device.DestroyFence(fence);throw; }
  device.DestroyFence(fence);
}

}
struct HDRIEnvironment::Impl
{
  Image source;
  mutable std::uint64_t revision{};
  mutable std::shared_ptr<const ImageData> pixels;
  struct Cached { std::uint64_t revision{}; std::shared_ptr<SceneTextureGPU> texture; };
  std::map<std::uint64_t, Cached> textures;
  std::map<std::tuple<std::uint64_t,unsigned,unsigned,unsigned>, Cached> specular;
  std::map<std::uint64_t,std::shared_ptr<SceneTextureGPU>> brdf;
  explicit Impl(Image image) : source(std::move(image)) {}
};
HDRIEnvironment::HDRIEnvironment(Image image) : m_impl(std::make_unique<Impl>(std::move(image))) {}
HDRIEnvironment::~HDRIEnvironment() = default;
std::uint64_t HDRIEnvironment::GetRevision() const noexcept { return m_impl->source.GetRevision(); }
std::shared_ptr<const ImageData> HDRIEnvironment::Resolve() const
{
  if (!m_impl->pixels || m_impl->revision != GetRevision()) {
    auto pixels = std::make_shared<ImageData>(m_impl->source.Resolve());
    pixels->Validate();
    ValidateHDR(*pixels);
    m_impl->pixels = std::move(pixels); m_impl->revision = GetRevision();
  }
  return m_impl->pixels;
}
std::shared_ptr<SceneTextureGPU> HDRIEnvironment::GetTexture(IRenderDevice& device)
{
  auto& cached = m_impl->textures[device.DeviceId()];
  if (cached.texture && cached.revision == GetRevision()) return cached.texture;
  auto pixels = Resolve();
  auto image = std::make_shared<SceneImageGPU>(); image->device = &device; image->resolution = pixels->GetResolution();
  image->format=pixels->format;image->mipLevels=pixels->GetRequiredMipCount();
  auto description=pixels->ToImageDesc();
  image->image = device.CreateImage(description);
  Detail::GenerateUploadedMipmaps(device, image->image, *pixels);
  image->view = device.CreateImageView({.image = image->image, .format = pixels->GetFormat(),.mipLevelCount=pixels->GetRequiredMipCount()});
  auto texture = std::make_shared<SceneTextureGPU>(); texture->device = &device;
  texture->sourceImage = image; texture->image = image->image; texture->view = image->view;
  texture->sampler = device.CreateSampler({.mipmap = image->mipLevels>1 ? SamplerMipmapFilter::Linear : SamplerMipmapFilter::None,
                                         .addressU = SamplerAddressMode::Repeat,
                                         .addressV = SamplerAddressMode::ClampToEdge});
  cached = {GetRevision(), texture}; return texture;
}
std::shared_ptr<SceneTextureGPU> HDRIEnvironment::GetPrefilteredTexture(IRenderDevice& device, UVec2 size, unsigned samples)
{
  if (!size.x || !size.y || size.x>4096 || size.y>4096 || !samples || samples>16384)
    throw std::invalid_argument("Invalid specular IBL filtering options");
  auto& cached=m_impl->specular[{device.DeviceId(),size.x,size.y,samples}];
  if (cached.texture && cached.revision==GetRevision()) return cached.texture;
  unsigned levels=1;
  // Keep angular resolution at the roughest level of a lat-long map.
  for (auto extent=std::max(size.x,size.y);extent>1 && (extent>16 || levels==1);extent/=2) ++levels;
  auto texture=MakeTexture(device,size,levels,ImageFormat::RGBA32f);
  FilterTexture(device,*texture,GetTexture(device),"environment/Prefilter.frag.glsl",samples);
  // Longitude wraps across the equirectangular seam; latitude clamps at poles.
  device.DestroySampler(texture->sampler);texture->sampler={};
  texture->sampler=device.CreateSampler({.mipmap=SamplerMipmapFilter::Linear,
    .addressU=SamplerAddressMode::Repeat,.addressV=SamplerAddressMode::ClampToEdge});
  cached={GetRevision(),texture};return texture;
}
std::shared_ptr<SceneTextureGPU> HDRIEnvironment::GetBRDFTexture(IRenderDevice& device)
{
  auto& texture=m_impl->brdf[device.DeviceId()];
  if (texture) return texture;
  auto result=MakeTexture(device,{128,128},1,ImageFormat::RGBA32f);
  FilterTexture(device,*result,{},"environment/BRDF.frag.glsl",1024);
  texture=result;return texture;
}
ImageData ComputeHDRIIrradiance(const ImageData& source, UVec2 size, std::uint32_t samples)
{
  ValidateHDR(source);
  if (!size.x || !size.y || size.x > 4096 || size.y > 4096 || !samples || samples > 65536)
    throw std::invalid_argument("Invalid irradiance resolution/sample count");
  std::vector<std::byte> pixels(std::size_t(size.x) * size.y * 4 * sizeof(float));
  for (unsigned y = 0; y < size.y; ++y) for (unsigned x = 0; x < size.x; ++x) {
    const auto n = Direction((x+0.5f)/size.x, (y+0.5f)/size.y);
    const auto tangent = glm::normalize(glm::cross(std::abs(n.y) < 0.999f ? Vec3(0,1,0) : Vec3(1,0,0), n));
    const auto bitangent = glm::cross(n, tangent);
    Vec3 sum(0);
    for (unsigned i = 0; i < samples; ++i) {
      const float u = (i + 0.5f)/samples, phi = 2*pi*RadicalInverse(i);
      const float r = std::sqrt(u);
      sum += Sample(source, tangent*(r*std::cos(phi)) + bitangent*(r*std::sin(phi)) + n*std::sqrt(1-u));
    }
    const std::array<float,4> value{sum.x*pi/samples, sum.y*pi/samples, sum.z*pi/samples, 1};
    std::memcpy(pixels.data()+(std::size_t(y)*size.x+x)*sizeof(value), value.data(), sizeof(value));
  }
  return {size, ImageFormat::RGBA32f, std::move(pixels)};
}
HDRISkyProvider::HDRISkyProvider(std::shared_ptr<HDRIEnvironment> environment) : m_environment(std::move(environment))
{ if (!m_environment) throw std::invalid_argument("Sky requires an HDRI environment"); }
HDRISkyProvider::~HDRISkyProvider() = default;
std::uint64_t HDRISkyProvider::GetConfigurationRevision() const noexcept { return m_environment->GetRevision(); }
void HDRISkyProvider::BuildGraphForward(ForwardRenderContext& context)
{
  BuildSky(context.frame,context.output);
}
void HDRISkyProvider::BuildGraphDeferred(DeferredRenderContext& context)
{
  BuildSky(context.frame,context.output);
}
void HDRISkyProvider::BuildSky(RenderFeatureContext& ctx, RenderFeatureResult& output)
{
  auto& device = ctx.technique.device;
  auto texture = m_environment->GetTexture(device);
  const auto input = ImportTexture(ctx, "HDRI sky radiance", texture);
  for (std::size_t view = 0; view < ctx.family->views.size(); ++view) {
    if (output.color[view].format != ImageFormat::RGBA16f || output.color[view].encoding != RenderColorEncoding::SceneLinear)
      throw std::logic_error("HDRI sky requires scene-linear RGBA16f attachments");
    auto state = std::make_shared<Bindings>(); state->device = &device;
    state->setLayout = device.CreateDescriptorSetLayout({.bindings = {
      {.binding=0, .type=DescriptorType::CombinedImageSampler, .count=1, .stages=ShaderStageFlags::Fragment},
      {.binding=1, .type=DescriptorType::UniformBuffer, .count=1, .stages=ShaderStageFlags::Fragment}}});
    state->layout = device.CreatePipelineLayout({.sets={state->setLayout}});
    state->vertex = device.CreateShaderModule(ShaderCompiler::Compile(ExtendedEmbedded::ReadText("environment/Sky.vert.glsl"), {.stage=ShaderStage::Vertex}));
    state->fragment = device.CreateShaderModule(ShaderCompiler::Compile(ExtendedEmbedded::ReadText("environment/Sky.frag.glsl"), {.stage=ShaderStage::Fragment}));
    state->pipeline = device.CreatePipeline({.vertexShader=state->vertex, .fragmentShader=state->fragment,
      .colorFormats={ImageFormat::RGBA16f}, .layout=state->layout});
    state->frame = device.CreateBuffer({.size=sizeof(Mat4)*2, .usage=BufferUsage::Uniform,
      .memory={.requiredAccess=MemoryAccess::CPUWrite|MemoryAccess::GPURead}});
    state->set = device.CreateDescriptorSet({.layout=state->setLayout});
    const std::array writes{
      DescriptorWrite{.binding=0, .resource=DescriptorImageInfo{texture->view,texture->sampler}},
      DescriptorWrite{.binding=1, .resource=DescriptorBufferInfo{state->frame,0,sizeof(Mat4)*2}}};
    device.UpdateDescriptorSet(state->set,writes);
    struct Data { RenderGraphResource color, radiance; };
    const auto& pass = ctx.technique.graph.AddPass<Data>("HDRI sky",
      [color=output.color[view].image,input](RenderGraph::Builder& builder,Data& data) {
        data.radiance=builder.Read(input); data.color=builder.Write(builder.Read(color));
      },
      [state,texture,family=ctx.family,view](const Data& data,FrameGraphPassResources& resources,RenderGraphExecutePassContext& execute) {
        const auto& camera=family->views[view];
        const std::array matrices{glm::inverse(camera.projection),glm::inverse(Mat4(glm::mat3(camera.view)))};
        WriteRetiredHostBuffer(*state->device,state->frame,std::as_bytes(std::span(matrices)));
        auto& commands=*execute.commandList;
        commands.MemoryBarrier({.srcStageMask=PipelineStage::ColorAttachmentOutput,
          .srcAccessMask=PipelineAccess::ColorAttachmentWrite,.dstStageMask=PipelineStage::ColorAttachmentOutput,
          .dstAccessMask=PipelineAccess::ColorAttachmentWrite});
        commands.BeginRendering({.colorAttachment=resources.get<RenderGraphImage>(data.color).viewHandle,
          .width=camera.resolution.x,.height=camera.resolution.y});
        commands.BindPipeline(state->pipeline); commands.BindDescriptorSet(0,state->set);
        commands.Draw(PrimitiveTopology::TriangleList,3,0); commands.EndRendering();
      });
    output.color[view].image=pass.color;
  }
}
struct HDRIIndirectLightingProvider::Impl
{
  std::shared_ptr<HDRIEnvironment> environment, irradiance;
  HDRIIndirectLightingOptions options;
  std::uint64_t revision{};
};
HDRIIndirectLightingProvider::HDRIIndirectLightingProvider(std::shared_ptr<HDRIEnvironment> environment,HDRIIndirectLightingOptions options)
  : m_impl(std::make_unique<Impl>())
{
  if (!environment || !options.specularResolution.x || !options.specularResolution.y ||
      options.specularResolution.x>4096 || options.specularResolution.y>4096 || !options.specularSamples || options.specularSamples>16384 ||
      (options.mode==IrradianceMode::Import && !options.irradiance) ||
      (options.mode!=IrradianceMode::Compute && options.mode!=IrradianceMode::Import) ||
      (options.mode==IrradianceMode::Compute && (!options.resolution.x || !options.resolution.y ||
      options.resolution.x>4096 || options.resolution.y>4096 || !options.samples || options.samples>65536)))
    throw std::invalid_argument("Invalid HDRI indirect lighting options");
  m_impl->environment=std::move(environment); m_impl->options=std::move(options);
}
HDRIIndirectLightingProvider::~HDRIIndirectLightingProvider() = default;
std::uint64_t HDRIIndirectLightingProvider::GetConfigurationRevision() const noexcept
{
  auto revision=m_impl->environment->GetRevision();
  if (m_impl->options.mode==IrradianceMode::Import)
    revision=revision*1099511628211ull ^ m_impl->options.irradiance->GetRevision();
  return revision;
}
void HDRIIndirectLightingProvider::BuildGraph(ForwardRenderContext& ctx)
{
  auto& impl=*m_impl;
  const auto revision=GetConfigurationRevision();
  if (!impl.irradiance || impl.revision!=revision) {
    if (impl.options.mode==IrradianceMode::Import)
      impl.irradiance=std::make_shared<HDRIEnvironment>(*impl.options.irradiance);
    else
      impl.irradiance=std::make_shared<HDRIEnvironment>(Image::FromGenerator(
        [environment=impl.environment,size=impl.options.resolution,samples=impl.options.samples] {
          return ComputeHDRIIrradiance(*environment->Resolve(),size,samples);
        }));
    impl.revision=revision;
  }
  auto& device=ctx.frame.technique.device;
  auto radiance=impl.environment->GetPrefilteredTexture(device,impl.options.specularResolution,impl.options.specularSamples);
  auto irradiance=impl.irradiance->GetTexture(device);
  auto brdf=impl.environment->GetBRDFTexture(device);
  ctx.lights.push_back(ImportTexture(ctx.frame,"IBL radiance",radiance));
  ctx.lights.push_back(ImportTexture(ctx.frame,"IBL diffuse irradiance",irradiance));
  ctx.lights.push_back(ImportTexture(ctx.frame,"IBL BRDF integration",brdf));
  auto state=std::make_shared<Bindings>(); state->device=&device;
  state->setLayout=device.CreateDescriptorSetLayout({.bindings={
    {.binding=0,.type=DescriptorType::CombinedImageSampler,.count=1,.stages=ShaderStageFlags::Fragment},
    {.binding=1,.type=DescriptorType::CombinedImageSampler,.count=1,.stages=ShaderStageFlags::Fragment},
    {.binding=2,.type=DescriptorType::CombinedImageSampler,.count=1,.stages=ShaderStageFlags::Fragment}}});
  state->set=device.CreateDescriptorSet({.layout=state->setLayout});
  const std::array writes{
    DescriptorWrite{.binding=0,.resource=DescriptorImageInfo{radiance->view,radiance->sampler}},
    DescriptorWrite{.binding=1,.resource=DescriptorImageInfo{irradiance->view,irradiance->sampler}},
    DescriptorWrite{.binding=2,.resource=DescriptorImageInfo{brdf->view,brdf->sampler}}};
  device.UpdateDescriptorSet(state->set,writes);
  ctx.indirectLayout=state->setLayout; ctx.indirectSet=state->set;
  ctx.indirectShader=ExtendedEmbedded::ReadText("environment/Indirect.glsl");
  ctx.frame.keepAlive.push_back(state);
}
void HDRIIndirectLightingProvider::BuildGraph(DeferredRenderContext& context)
{
  // The environment prefilter/bindings are shared; compatibility remains an
  // explicit implementation of each feature's provider interface.
  ForwardRenderContext resources{context.frame};
  BuildGraph(resources);
  context.lights.insert(context.lights.end(), resources.lights.begin(), resources.lights.end());
  context.indirectShader = std::move(resources.indirectShader);
  for(std::size_t at=0;(at=context.indirectShader.find("set=3",at))!=std::string::npos;at+=5)
    context.indirectShader.replace(at,5,"set=2");
  context.indirectLayout = resources.indirectLayout;
  context.indirectSet = resources.indirectSet;
}
DeferredDependencies HDRIIndirectLightingProvider::GetDeferredDependencies() const
{
  return {{DeferredSemantics::Normal, DeferredSemantics::WorldPosition,
           DeferredSemantics::SurfaceProperties}, {}};
}
} // namespace Lighthouse
