#include <Lighthouse/Extended/Rendering/EnvironmentProviders.hpp>
#include <Lighthouse/Extended/Rendering/ForwardRendering.hpp>
#include <Lighthouse/Engine/Assets/ShaderCompiler.hpp>
#include <Lighthouse/Backends/Render/OpenGL/OpenGLBackend.hpp>
#include <Lighthouse/Backends/Render/Vulkan/VulkanBackend.hpp>
#include "QueueTestHelpers.hpp"
#include <Lighthouse/Extended/Rendering/Renderer.hpp>
#include <Lighthouse/Extended/Rendering/SceneProvider.hpp>
#include <Lighthouse/Extended/World/Entity/Transform.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <fstream>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <iostream>
using namespace Lighthouse;
namespace {
void Check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
ImageMipData Constant(unsigned width,unsigned height,Vec4 value) {
  ImageMipData mip{width,height,1,std::vector<std::byte>(width*height*16)};
  for(std::size_t i=0;i<mip.data.size();i+=16)std::memcpy(mip.data.data()+i,&value,16);
  return mip;
}
ImageData RequestMipmaps(ImageData image) { return ApplyImageMipPolicy(std::move(image), MipPolicy::Generate); }
std::shared_ptr<SceneTextureGPU> Upload(IRenderDevice& device,const ImageData& data) {
  auto image=std::make_shared<SceneImageGPU>();image->device=&device;
  image->resolution=data.GetResolution();image->format=data.format;image->mipLevels=data.GetRequiredMipCount();
  image->image=device.CreateImage(data.ToImageDesc());
  if(data.generateMipmaps) {
    auto commands=device.CreateCommandList(0);commands->Begin();
    commands->GenMipMaps(GenMipMapsDesc{.image=image->image});commands->End();
    SubmitAndWait(device,*commands);
  }
  image->view=device.CreateImageView({.image=image->image,.format=data.format,.type=data.GetDepth()>1 ? ImageType::Image3D : ImageType::Image2D,.mipLevelCount=image->mipLevels});
  auto texture=std::make_shared<SceneTextureGPU>();texture->device=&device;texture->sourceImage=image;
  texture->image=image->image;texture->view=image->view;
  texture->sampler=device.CreateSampler({.mipmap=SamplerMipmapFilter::Linear});return texture;
}
std::vector<std::byte> Sample(IRenderDevice& device,const SceneTextureGPU& texture,float lod,float scale=1,bool volume=false) {
  struct State {
    IRenderDevice& device;ImageHandle image{};ImageViewHandle view{};
    DescriptorSetLayoutHandle setLayout{};DescriptorSetHandle set{};PipelineLayoutHandle layout{};
    GraphicsPipelineHandle pipeline{};ShaderModuleHandle vertex{},fragment{};BufferHandle frame{};
    ~State(){
      if(pipeline)device.DestroyPipeline(pipeline);
      if(vertex)device.DestroyModule(vertex);if(fragment)device.DestroyModule(fragment);
      if(layout)device.DestroyPipelineLayout(layout);if(set)device.DestroyDescriptorSet(set);
      if(setLayout)device.DestroyDescriptorSetLayout(setLayout);if(frame)device.DestroyBuffer(frame);
      if(view)device.DestroyImageView(view);if(image)device.DestroyImage(image);
    }
  } state{device};
  state.image=device.CreateImage({.width=64,.height=32,.format=ImageFormat::RGBA8,
    .usage={ImageUsage::ColorAttachment,ImageUsage::TransferSource}});
  state.view=device.CreateImageView({.image=state.image,.format=ImageFormat::RGBA8});
  state.setLayout=device.CreateDescriptorSetLayout({.bindings={
    {.binding=0,.type=DescriptorType::CombinedImageSampler,.count=1,.stages=ShaderStageFlags::Fragment},
    {.binding=1,.type=DescriptorType::UniformBuffer,.count=1,.stages=ShaderStageFlags::Fragment}}});
  const Vec4 settings(lod,scale,0,0);
  state.frame=device.CreateBuffer({.size=sizeof(settings),.usage=BufferUsage::Uniform,.initialData=std::as_bytes(std::span(&settings,1))});
  state.set=device.CreateDescriptorSet({.layout=state.setLayout});
  const std::array writes{
    DescriptorWrite{.binding=0,.resource=DescriptorImageInfo{texture.view,texture.sampler}},
    DescriptorWrite{.binding=1,.resource=DescriptorBufferInfo{state.frame,0,sizeof(settings)}}};
  device.UpdateDescriptorSet(state.set,writes);
  state.layout=device.CreatePipelineLayout({.sets={state.setLayout}});
  state.vertex=device.CreateShaderModule(ShaderCompiler::Compile(R"glsl(#version 450
void main(){vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));gl_Position=vec4(p[gl_VertexIndex],0,1);}
)glsl",{.stage=ShaderStage::Vertex}));
  std::string fragmentSource=R"glsl(#version 450
layout(set=0,binding=0) uniform sampler2D source;
layout(set=0,binding=1,std140) uniform Settings { vec4 value; } settings;
layout(location=0) out vec4 color;
void main(){color=vec4(textureLod(source,gl_FragCoord.xy/vec2(64,32),settings.value.x).rgb*settings.value.y,1);}
)glsl";
  if(volume) {
    fragmentSource.replace(fragmentSource.find("sampler2D"),9,"sampler3D");
    const std::string coordinate="gl_FragCoord.xy/vec2(64,32)";
    fragmentSource.replace(fragmentSource.find(coordinate),coordinate.size(),"vec3(gl_FragCoord.xy/vec2(64,32),0.5)");
  }
  state.fragment=device.CreateShaderModule(ShaderCompiler::Compile(fragmentSource,{.stage=ShaderStage::Fragment}));
  state.pipeline=device.CreatePipeline({.vertexShader=state.vertex,.fragmentShader=state.fragment,
    .colorFormats={ImageFormat::RGBA8},.layout=state.layout});
  auto commands=device.CreateCommandList(0);commands->Begin();
  commands->ImageBarrier({.image=state.image,.oldLayout=ImageLayout::Undefined,.newLayout=ImageLayout::ColorAttachment});
  commands->BeginRendering({.colorAttachment=state.view,.width=64,.height=32});
  commands->BindPipeline(state.pipeline);commands->BindDescriptorSet(0,state.set);
  commands->Draw(PrimitiveTopology::TriangleList,3,0);commands->EndRendering();commands->End();
  SubmitAndWait(device,*commands);return device.ReadImage(state.image);
}
std::pair<double,double> Statistics(const std::vector<std::byte>& pixels) {
  double sum=0,sum2=0;for(std::size_t i=0;i<pixels.size();i+=4){const auto value=std::to_integer<int>(pixels[i])/255.0;sum+=value;sum2+=value*value;}
  const auto count=pixels.size()/4;return {sum/count,sum2/count-(sum/count)*(sum/count)};
}
void Run(IRenderDevice& device) {
  // Distinct authored colors prove that every level is resident and exposed.
  ImageData bytes(ImageFormat::RGBA8,{{4,2,1,std::vector<std::byte>(32)},
                                     {2,1,1,{std::byte{0},std::byte{255},std::byte{0},std::byte{255},
                                             std::byte{0},std::byte{255},std::byte{0},std::byte{255}}}});
  auto texture=Upload(device,bytes);auto green=Sample(device,*texture,1);
  Check(green[0]==std::byte{0} && green[1]==std::byte{255},"RGBA8 mip one was not uploaded/exposed");
  ImageData floats(ImageFormat::RGBA32f,{Constant(4,2,{4,0,0,1}),Constant(2,1,{0,0,2,1})});
  auto hdr=Upload(device,floats);auto blue=Sample(device,*hdr,1,0.25f);
  Check(blue[0]==std::byte{0} && std::abs(std::to_integer<int>(blue[2])-128)<=1,"Float HDR mip upload lost values or selected mip zero");
  // GPU generation must replace authored lower levels, preserve HDR range, and
  // make every generated level visible to subsequent shader sampling.
  auto generated=Upload(device,RequestMipmaps(floats));
  for(float lod : {1.0f,2.0f}) {
    const auto red=Sample(device,*generated,lod,0.125f);
    Check(std::abs(std::to_integer<int>(red[0])-128)<=1 && red[2]==std::byte{0},"Generated HDR chain is incorrect");
  }
  // Re-recording the same operation tests tracked layouts after earlier sampling.
  auto regenerate=device.CreateCommandList(0);regenerate->Begin();
  regenerate->GenMipMaps(GenMipMapsDesc{.image=generated->image});regenerate->End();
  SubmitAndWait(device,*regenerate);
  Check(std::abs(std::to_integer<int>(Sample(device,*generated,2,0.125f)[0])-128)<=1,"Repeated mip generation failed");
  std::vector<std::byte> checkerBytes(8,std::byte{255});
  checkerBytes[0]=checkerBytes[1]=checkerBytes[2]=std::byte{0};
  auto srgb=Upload(device,RequestMipmaps(ImageData({2,1},ImageFormat::RGBA8Srgb,checkerBytes)));
  Check(std::abs(std::to_integer<int>(Sample(device,*srgb,1)[0])-128)<=2,"sRGB mip generation did not filter in linear space");
  auto odd=Upload(device,RequestMipmaps(ImageData(ImageFormat::RGBA32f,{Constant(7,3,{2,0,0,1})})));
  Check(std::abs(std::to_integer<int>(Sample(device,*odd,2,0.25f)[0])-128)<=1,"Odd-sized mip chain failed");
  auto single=Upload(device,RequestMipmaps(ImageData(ImageFormat::RGBA32f,{Constant(1,1,{2,0,0,1})})));
  Check(std::abs(std::to_integer<int>(Sample(device,*single,0,0.25f)[0])-128)<=1,"Single-level generation failed");
  ImageMipData volumeMip{2,2,2,std::vector<std::byte>(8*16)};
  for(unsigned i=0;i<8;++i) {
    const Vec4 value(i<4?0.0f:4.0f,0,0,1);
    std::memcpy(volumeMip.data.data()+i*16,&value,16);
  }
  auto volume=Upload(device,RequestMipmaps(ImageData(ImageFormat::RGBA32f,{volumeMip})));
  Check(std::abs(std::to_integer<int>(Sample(device,*volume,1,0.25f,true)[0])-128)<=1,"3D mip generation failed");
  // Strong high-frequency variation must blur as GGX roughness increases.
  auto checker=Constant(64,32,{0,0,0,1});
  for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x){
    const auto value=((x/4+y/4)%2)?1.0f:0.1f;const Vec4 pixel(value,value,value,1);
    std::memcpy(checker.data.data()+(y*64+x)*16,&pixel,16);
  }
  auto source=Image::FromData(RequestMipmaps(ImageData(ImageFormat::RGBA32f,{checker})));
  HDRIEnvironment environment(source);
  auto filtered=environment.GetPrefilteredTexture(device,{64,32},512);
  Check(filtered->sourceImage->mipLevels>=3,"Specular filtering did not create roughness levels");
  const auto sharp=Statistics(Sample(device,*filtered,0));
  const auto rough=Statistics(Sample(device,*filtered,float(filtered->sourceImage->mipLevels-1)));
  Check(rough.second<sharp.second*0.25,"Rough specular mip retained sharp structured reflections");
  Check(rough.first>0.35 && rough.first<0.75,"GGX filtering lost environment energy");
  Check(filtered==environment.GetPrefilteredTexture(device,{64,32},512),"Specular convolution was not cached");
  source.MarkDirty();Check(filtered!=environment.GetPrefilteredTexture(device,{64,32},512),"Source change did not invalidate specular mips");
  auto constantSource=Image::FromData(RequestMipmaps(ImageData(ImageFormat::RGBA32f,{Constant(64,32,{4,1,0.25f,1})})));
  HDRIEnvironment constant(constantSource);
  const auto constantTexture=constant.GetPrefilteredTexture(device,{64,32},128);
  for(unsigned mip=0;mip<constantTexture->sourceImage->mipLevels;++mip){
    const auto pixels=Sample(device,*constantTexture,float(mip),0.25f);
    for(std::size_t i=0;i<pixels.size();i+=4)
      Check(std::abs(std::to_integer<int>(pixels[i])-255)<=1 && std::abs(std::to_integer<int>(pixels[i+1])-64)<=1,
        "GGX roughness filtering changed constant HDR radiance");
  }
  auto lut=constant.GetBRDFTexture(device);Check(lut==constant.GetBRDFTexture(device),"BRDF LUT was not cached");
  const auto dfg=Sample(device,*lut,0);Check(Statistics(dfg).first>0.1,"BRDF integration LUT is empty");
  std::cout<<"RGBA8/float mip uploads, roughness blur, energy and IBL cache passed\n";
}
void RenderRoughness(std::unique_ptr<IRenderDevice> device) {
  RenderGraphComposer composer(std::move(device));auto& gpu=*composer.GetRenderDevice();
  Scene scene("Roughness regression");auto store=std::make_shared<SceneRenderStore>(scene);
  Mesh3D mesh;constexpr unsigned slices=96,rings=48;constexpr float pi=3.141592653589793;
  for(unsigned y=0;y<=rings;++y)for(unsigned x=0;x<=slices;++x){
    const float theta=float(y)*pi/rings,phi=float(x)*2*pi/slices;
    const Vec3 normal(std::sin(theta)*std::cos(phi),std::cos(theta),std::sin(theta)*std::sin(phi));
    mesh.vertices.push_back({normal,normal,{float(x)/slices,float(y)/rings}});
  }
  for(unsigned y=0;y<rings;++y)for(unsigned x=0;x<slices;++x){
    const auto a=y*(slices+1)+x,b=a+slices+1;
    mesh.indices.insert(mesh.indices.end(),{a,b,a+1,a+1,b,b+1});
  }
  auto sphere=scene.CreateEntity("Metal sphere");sphere.Add<Transform>();
  auto& draw=sphere.Add<MeshRender>();draw.mesh=ModelResource::FromData(std::move(mesh));
  const auto definition=std::filesystem::path(__FILE__).parent_path()/"Fixtures/SpecularSurface.material.json";
  draw.material=MaterialResource::FromDefinition(definition,{{"roughness",0.1f}});
  auto checker=Constant(128,64,{0,0,0,1});
  for(unsigned y=0;y<64;++y)for(unsigned x=0;x<128;++x){
    const auto value=((x/8+y/8)%2)?1.0f:0.03f;const Vec4 pixel(value,value,value,1);
    std::memcpy(checker.data.data()+(y*128+x)*16,&pixel,16);
  }
  auto environment=std::make_shared<HDRIEnvironment>(Image::FromData(
    RequestMipmaps(ImageData(ImageFormat::RGBA32f,{std::move(checker)}))));
  const auto image=gpu.CreateImage({.width=256,.height=256,.format=ImageFormat::RGBA8,
    .usage={ImageUsage::ColorAttachment,ImageUsage::TransferDestination,ImageUsage::TransferSource}});
  const auto imageView=gpu.CreateImageView({.image=image,.format=ImageFormat::RGBA8});
  const auto ref=composer.AddTechnique<Renderer>(&composer);auto* renderer=composer.GetTechnique<Renderer>(ref);
  auto& view=renderer->CreateView(std::make_shared<GraphRenderOutput>([image,imageView](TechniqueBuildContext& ctx,const RenderViewFamily& family){
    const auto resource=ctx.graph.Import("Roughness output",ImportedRenderGraphImage::Desc{256,256,ImageFormat::RGBA8,
      ImageUsage::ColorAttachment|ImageUsage::TransferDestination|ImageUsage::TransferSource},ImportedRenderGraphImage{imageView,image,{},ctx.device.GetImageLayout(image)});
    return RenderOutputTargets{{family.views[0].id,resource,0,0,{0,0,256,256},ImageFormat::RGBA8,RenderColorEncoding::SRGB,RenderTargetKind::ImportedImage}};
  }));
  auto& feature=view.SetRenderFeature<ForwardRenderFeature>();feature.AddProvider<SceneProvider>(store);
  HDRIIndirectLightingOptions options;options.resolution={16,8};options.samples=64;
  options.specularResolution={128,64};options.specularSamples=512;
  feature.SetIndirectLightingProvider<HDRIIndirectLightingProvider>(environment,options);
  RenderViewData camera;camera.id=1;camera.resolution={256,256};camera.cameraPosition={0,0,3};
  camera.view=glm::lookAtRH(camera.cameraPosition,Vec3(0),Vec3(0,1,0));
  camera.projection=glm::perspectiveRH_ZO(glm::radians(45.0f),1.0f,0.1f,100.0f);view.SetViewFamily({{camera}});
  renderer->Update(0.016f);composer.Compile();composer.Render();gpu.WaitIdle();const auto sharp=gpu.ReadImage(image);
  draw.material=MaterialResource::FromDefinition(definition,{{"roughness",0.85f}});
  renderer->Update(0.016f);Check(renderer->NeedsRebuild(),"Roughness material change was not detected");
  composer.Compile();composer.Render();gpu.WaitIdle();const auto rough=gpu.ReadImage(image);
  const auto gradient=[](const std::vector<std::byte>& pixels){
    double total=0;for(unsigned y=64;y<192;++y)for(unsigned x=64;x<192;++x){
      const auto i=(y*256+x)*4;
      total+=std::abs(std::to_integer<int>(pixels[i])-std::to_integer<int>(pixels[i+4]));
      total+=std::abs(std::to_integer<int>(pixels[i])-std::to_integer<int>(pixels[i+256*4]));
    }return total;
  };
  const auto sharpGradient=gradient(sharp),roughGradient=gradient(rough);
  Check(sharpGradient>10000 && roughGradient<sharpGradient*0.6,"Material roughness did not smooth specular reflections");
  if(const char* path=std::getenv("LIGHTHOUSE_ROUGHNESS_TEST_DUMP")){
    std::ofstream output(path,std::ios::binary);output<<"P6\n512 256\n255\n";
    for(unsigned y=0;y<256;++y)for(const auto* pixels:{&sharp,&rough})for(unsigned x=0;x<256;++x)
      output.write(reinterpret_cast<const char*>(pixels->data()+(y*256+x)*4),3);
  }
  std::cout<<"Metal sphere roughness gradients: "<<sharpGradient<<" -> "<<roughGradient<<'\n';
  composer.GetRenderGraph().Clear();composer.RemoveTechnique(ref.handle);
  gpu.DestroyImageView(imageView);gpu.DestroyImage(image);
}

}
int main(int argc,char** argv) {
  try {
    const bool vulkan=argc>1 && std::string_view(argv[1])=="--vulkan";
    std::unique_ptr<IGraphicsBackend> backend;
    if(vulkan)backend=std::make_unique<VulkanBackend>();else backend=std::make_unique<OpenGLBackend>();
    std::string diagnostic;Check(backend->Initialize(diagnostic),diagnostic.c_str());
    { auto device=backend->CreateDevice({.applicationName="Mip/roughness test",
      .queues={{.capabilities=RenderQueueCapability::Graphics|RenderQueueCapability::Compute|RenderQueueCapability::Transfer,.minCount=1}}});
      Run(*device);RenderRoughness(std::move(device)); }
    backend->Shutdown();return 0;
  } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
