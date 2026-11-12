#include <Lighthouse/Extended/Rendering/Renderer.hpp>
#include <Lighthouse/Extended/Rendering/Bloom.hpp>
#include <Lighthouse/Extended/Rendering/DeferredRendering.hpp>
#include <Lighthouse/Extended/Rendering/SceneProvider.hpp>
#include <Lighthouse/Extended/Rendering/ForwardRendering.hpp>
#include <Lighthouse/Extended/Resources/Resources.hpp>
#include <Lighthouse/Extended/World/Entity/Light.hpp>
#include <Lighthouse/Extended/World/Entity/Transform.hpp>
#include <Lighthouse/Backends/Render/OpenGL/OpenGLBackend.hpp>
#include <Lighthouse/Backends/Render/Vulkan/VulkanBackend.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <Lighthouse/Extended/Rendering/EnvironmentProviders.hpp>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <cstring>
#ifdef LIGHTHOUSE_TEST_DEFERRED
#include "DeferredBufferProbe.hpp"
#endif
using namespace Lighthouse;
#ifdef LIGHTHOUSE_TEST_DEFERRED
using RasterFeature=DeferredRenderFeature;
using RasterSceneProvider=SceneDeferredRenderProvider;
using RasterLightingProvider=DefaultDeferredLightingProvider;
using RasterIndirectProvider=IDeferredIndirectLightingProvider;
using RasterContext=DeferredRenderContext;
#else
using RasterFeature=ForwardRenderFeature;
using RasterSceneProvider=SceneProvider;
using RasterLightingProvider=DefaultForwardLightingProvider;
using RasterIndirectProvider=IForwardIndirectLightingProvider;
using RasterContext=ForwardRenderContext;
#endif
Bloom& AddBloom(RasterFeature& feature,BloomSettings settings)
{
#ifdef LIGHTHOUSE_TEST_DEFERRED
  return feature.AddDeferredPostProcess<Bloom>(settings);
#else
  return feature.AddForwardPostProcess<Bloom>(settings);
#endif
}
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv)
{
  try {
    const bool vulkan=argc>1 && std::string_view(argv[1])=="--vulkan";
    std::unique_ptr<IGraphicsBackend> backend;
    if(vulkan)backend=std::make_unique<VulkanBackend>();else backend=std::make_unique<OpenGLBackend>();
    std::string diagnostic;if(!backend->Initialize(diagnostic))throw std::runtime_error(diagnostic);
    {
      auto device=backend->CreateDevice({.applicationName="Forward scene test",.queues={{.capabilities=RenderQueueCapability::Graphics|RenderQueueCapability::Compute|RenderQueueCapability::Transfer,.minCount=1}}});
      RenderGraphComposer composer(std::move(device));auto& gpu=*composer.GetRenderDevice();
      const auto resource=Resource::LoadScene(FORWARD_TEST_MODEL);
      auto scene=resource.GetDefaultScene();auto store=std::make_shared<SceneRenderStore>(scene);
      auto key=scene.CreateEntity("key");key.Add<Transform>();key.Add<Light>().intensity=5;
      auto fill=scene.CreateEntity("fill");fill.Add<Transform>().SetRotEuler(glm::radians(Vec3(20,40,0)));fill.Add<Light>().intensity=3;
      std::vector<std::byte> background(256*256*4);
      for(std::size_t i=0;i<background.size();i+=4){background[i]=std::byte{9};background[i+1]=std::byte{13};background[i+2]=std::byte{19};background[i+3]=std::byte{255};}
      const auto image=gpu.CreateImage({.width=256,.height=256,.format=ImageFormat::RGBA8,
        .usage={ImageUsage::ColorAttachment,ImageUsage::TransferSource,ImageUsage::TransferDestination},.initialData=background});
      const auto imageView=gpu.CreateImageView({.image=image,.format=ImageFormat::RGBA8});
      {
        auto ref=composer.AddTechnique<Renderer>(&composer);auto* renderer=composer.GetTechnique<Renderer>(ref);
        auto& view=renderer->CreateView(std::make_shared<GraphRenderOutput>([image,imageView,vulkan](TechniqueBuildContext& ctx,const RenderViewFamily& family){
          const auto imageResource=ctx.graph.Import("Test output",ImportedRenderGraphImage::Desc{256,256,ImageFormat::RGBA8,
            ImageUsage::ColorAttachment|ImageUsage::TransferDestination|ImageUsage::TransferSource},ImportedRenderGraphImage{imageView,image});
          return RenderOutputTargets{{family.views[0].id,imageResource,0,0,{0,vulkan?0u:128u,128,128},ImageFormat::RGBA8,RenderColorEncoding::SRGB,RenderTargetKind::ImportedImage}};
        }));
        auto& feature=view.SetRenderFeature<RasterFeature>();
        feature.AddProvider<RasterSceneProvider>(store);feature.SetLighting<RasterLightingProvider>(store);
        const char* hdriPath=std::getenv("LIGHTHOUSE_TEST_HDRI");
        if(hdriPath) {
          auto environment=std::make_shared<HDRIEnvironment>(Resource::LoadImage(hdriPath));
          feature.SetEnvironmentProvider<EnvironmentProvider>(std::make_shared<HDRISkyProvider>(environment));
          HDRIIndirectLightingOptions options;
          options.specularResolution={64,32};options.specularSamples=256;
          if(const char* irr=std::getenv("LIGHTHOUSE_TEST_IRRADIANCE")) {
            options.mode=IrradianceMode::Import;options.irradiance=Resource::LoadImage(irr);
          }
          feature.SetIndirectLightingProvider<HDRIIndirectLightingProvider>(environment,options);
        }
        RenderViewData camera;camera.id=1;camera.resolution={128,128};camera.cameraPosition={0,0,3.5f};
        camera.view=glm::lookAtRH(camera.cameraPosition,Vec3(0,-0.15f,0),Vec3(0,1,0));
        camera.projection=glm::perspectiveRH_ZO(glm::radians(45.0f),1.0f,0.1f,100.0f);
        view.SetViewFamily({{camera}});renderer->Update(0.016f);composer.Compile();composer.Render();gpu.WaitIdle();
        const auto pixels=gpu.ReadImage(image);Check(pixels.size()==background.size(),"Image readback failed");
        const std::size_t firstRow=vulkan?0:128;
        std::size_t changed=0,bright=0;
        for(std::size_t y=0;y<256;++y)for(std::size_t x=0;x<256;++x){
          const auto i=(y*256+x)*4;const bool inside=x<128&&y>=firstRow&&y<firstRow+128;
          if(!inside)Check(pixels[i]==background[i]&&pixels[i+1]==background[i+1]&&pixels[i+2]==background[i+2],"Forward rendering overwrote another quadrant");
          else{if(pixels[i]!=background[i])++changed;if(std::to_integer<int>(pixels[i])>75||std::to_integer<int>(pixels[i+1])>75)++bright;}
        }
        Check(changed>1000&&bright>250,"Helmet did not produce visible lit pixels");
        const auto model=resource.GetModels().front();const auto material=resource.GetMaterials().front();
        Check(store->GetMesh(gpu,model)==store->GetMesh(gpu,model),"Mesh allocation was duplicated");
        Check(store->GetMaterial(gpu,material)==store->GetMaterial(gpu,material),"Material allocation was duplicated");
        if(const char* path=std::getenv("LIGHTHOUSE_FORWARD_TEST_DUMP")){
          std::ofstream dump(path,std::ios::binary);dump<<"P6\n256 256\n255\n";
          for(std::size_t y=0;y<256;++y){const auto row=vulkan?y:255-y;for(std::size_t x=0;x<256;++x)dump.write(reinterpret_cast<const char*>(pixels.data()+(row*256+x)*4),3);}
        }
        key.Get<Light>().enabled=false;fill.Get<Light>().enabled=false;
        renderer->Update(0.016f);Check(!renderer->NeedsRebuild(),"Changing light values unnecessarily rebuilt the graph");
        composer.Render();gpu.WaitIdle();const auto unlit=gpu.ReadImage(image);
        std::size_t difference=0;for(std::size_t i=0;i<pixels.size();++i)difference+=std::abs(std::to_integer<int>(pixels[i])-std::to_integer<int>(unlit[i]));
        Check(difference>10000,"Scene light changes did not affect shading");
        if(hdriPath) {
          // Removing only IBL must change helmet pixels while retaining the sky.
          std::ofstream dump;
          if(const char* path=std::getenv("LIGHTHOUSE_IBL_TEST_DUMP")) {
            dump.open(path,std::ios::binary);dump<<"P6\n256 256\n255\n";
            for(std::size_t y=0;y<256;++y){const auto row=vulkan?y:255-y;for(std::size_t x=0;x<256;++x)dump.write(reinterpret_cast<const char*>(unlit.data()+(row*256+x)*4),3);}
          }
          struct ZeroIndirect final : RasterIndirectProvider { void BuildGraph(RasterContext&) override {} };
          feature.SetIndirectLightingProvider<ZeroIndirect>();
          renderer->Update(0.016f);Check(renderer->NeedsRebuild(),"Provider replacement did not invalidate the graph");
          composer.Compile();composer.Render();gpu.WaitIdle();const auto noIBL=gpu.ReadImage(image);
          std::size_t changedIBL=0;
          for(std::size_t y=firstRow+32;y<firstRow+96;++y)for(std::size_t x=32;x<96;++x) {
            const auto i=(y*256+x)*4;
            changedIBL+=std::abs(std::to_integer<int>(unlit[i])-std::to_integer<int>(noIBL[i]));
          }
          Check(changedIBL>1000,"HDRI IBL did not light the helmet with direct lights disabled");
        }
        // Material definitions, not a renderer-local PBR shader, compile every path.
        Materials materialSystem(gpu);
        const auto customPath=std::filesystem::path(__FILE__).parent_path()/"Fixtures/SharedSurface.material.json";
        for (const auto& definition : {materialSystem.Load("materials/GltfPBR.material.json"),
             materialSystem.Load("materials/SolidColor.glsl"),materialSystem.Load(customPath)}) {
          const auto forward=definition->Compile(MaterialRenderPath::Forward);
          const auto deferred=definition->Compile(MaterialRenderPath::Deferred);
          const auto ray=definition->Compile(MaterialRenderPath::RayTracing);
          Check(forward.vertex&&forward.fragment&&!forward.closestHit,"Forward material target failed");
          Check(deferred.vertex&&deferred.fragment&&!deferred.closestHit,"Deferred material target failed");
          Check(ray.closestHit&&!ray.vertex&&!ray.fragment,"Ray material target failed");
          Check(ray.closestHit->GetStage()==ShaderStage::ClosestHit,"Ray material stage mismatch");
        }
        auto assign=[&](MaterialHandle material) {
          for(auto entity:scene.Registry().view<MeshRender>())scene.Registry().get<MeshRender>(entity).material=material;
          renderer->Update(0.016f);Check(renderer->NeedsRebuild(),"Material change was not detected");
          composer.Compile();composer.Render();gpu.WaitIdle();return gpu.ReadImage(image);
        };
        const auto black=assign(MaterialResource::FromDefinition(customPath,{{"emission",Vec3(0)}}));
        const auto center=((firstRow+64)*256+64)*4;
        Check(black[center]==std::byte{0} && black[center+1]==std::byte{0} && black[center+2]==std::byte{0},
              "Forward material retained implicit ambient without lights or IBL");
        const auto custom=assign(MaterialResource::FromDefinition(customPath));
        const auto white=assign(MaterialResource::FromDefinition("materials/SolidColor.glsl"));
        difference=0;for(std::size_t i=0;i<white.size();++i)difference+=std::abs(std::to_integer<int>(custom[i])-std::to_integer<int>(white[i]));
        Check(difference>100000,"Custom Lit and Unlit definitions did not affect forward pixels");
        key.Get<Light>().enabled=true;fill.Get<Light>().enabled=true;
        renderer->Update(0.016f);composer.Render();gpu.WaitIdle();
        const auto whiteLit=gpu.ReadImage(image);
        Check(white==whiteLit,"Unlit shading model incorrectly used scene lighting");
        // Always verify real float uploads, including values above one, with
        // a sky-only feature (no geometry, direct lighting or IBL providers).
        std::vector<std::byte> constantBytes(8*4*16);
        const std::array<float,4> constant{4.0f,1.0f,0.25f,1.0f};
        for(std::size_t i=0;i<constantBytes.size();i+=16)std::memcpy(constantBytes.data()+i,constant.data(),16);
        auto constantImage=Image::FromData(ImageData({8,4},ImageFormat::RGBA32f,std::move(constantBytes)));
        auto constantEnvironment=std::make_shared<HDRIEnvironment>(constantImage);
        auto& skyOnly=view.SetRenderFeature<RasterFeature>();
        skyOnly.SetEnvironmentProvider<EnvironmentProvider>(std::make_shared<HDRISkyProvider>(constantEnvironment));
        renderer->Update(0.016f);composer.Compile();composer.Render();gpu.WaitIdle();
        const auto hdrPixels=gpu.ReadImage(image);
        const auto index=((firstRow+64)*256+64)*4;
        const auto expected=[](float value){return int(std::lround((1.055f*std::pow(value/(1+value),1/2.4f)-0.055f)*255));};
        for(int channel=0;channel<3;++channel)
          Check(std::abs(std::to_integer<int>(hdrPixels[index+channel])-expected(constant[channel]))<=2,
                "HDR sky upload or tone mapping lost float radiance");
        constantImage.MarkDirty();renderer->Update(0.016f);
        Check(renderer->NeedsRebuild(),"HDR source revision did not invalidate the graph");
        composer.Compile();composer.Render();gpu.WaitIdle();
        Check(hdrPixels==gpu.ReadImage(image),"Re-upload changed constant HDR pixels");
        // Bloom is entirely GPU-rendered and remains in HDR until resolve.
        auto& bloom = AddBloom(skyOnly,BloomSettings{.Intensity=0.5f,.MipCount=5});
        Check(renderer->NeedsRebuild(), "Adding bloom did not invalidate the graph");
        composer.Compile();
        const auto bloomGraph=composer.GetRenderGraph().GetInspectionData(true);
        std::size_t compositeIndex=0,resolveIndex=0;
        for(std::size_t i=0;i<bloomGraph.passes.size();++i) {
          if(bloomGraph.passes[i].name=="Bloom composite")compositeIndex=i;
          if(bloomGraph.passes[i].name=="Resolve scene view")resolveIndex=i;
        }
        Check(compositeIndex<resolveIndex,"Bloom must precede tone mapping");
        composer.Render();gpu.WaitIdle();
        auto bloomPixels=gpu.ReadImage(image);
        for(int channel=0;channel<3;++channel)
          Check(std::abs(std::to_integer<int>(bloomPixels[index+channel])-expected(constant[channel]*1.375f))<=3,
                "GPU bloom composite differs from expected HDR result");
        auto bloomSettings=bloom.GetSettings(); bloomSettings.Intensity=0;
        bloom.SetSettings(bloomSettings);renderer->Update(0.016f);
        Check(!renderer->NeedsRebuild(),"Bloom intensity unnecessarily rebuilt the graph");
        composer.Render();gpu.WaitIdle();
        Check(hdrPixels==gpu.ReadImage(image),"Zero bloom intensity changed the scene");
        bloomSettings.Enabled=false; bloom.SetSettings(bloomSettings);renderer->Update(0.016f);
        Check(renderer->NeedsRebuild(),"Disabling bloom did not invalidate graph structure");
        composer.Compile();composer.Render();gpu.WaitIdle();
        Check(hdrPixels==gpu.ReadImage(image),"Disabled bloom changed the scene");
        bloomSettings.Enabled=true;bloomSettings.Intensity=0.5f;bloomSettings.MipCount=32;
        bloom.SetSettings(bloomSettings);
        for(const UVec2 size : {UVec2(63,35),UVec2(1,1),UVec2(1,7)}) {
          camera.resolution=size;view.SetViewFamily({{camera}});renderer->Update(0.016f);
          composer.Compile();composer.Render();gpu.WaitIdle();
          const auto tinyPixels=gpu.ReadImage(image);
          for(int channel=0;channel<3;++channel)
            Check(std::abs(std::to_integer<int>(tinyPixels[index+channel])-expected(constant[channel]*1.375f))<=3,
                  "Bloom failed for odd or small viewport dimensions");
        }
        // A localized emissive mesh must cast bloom into neighboring dark pixels.
        MaterialAsset glowMaterial;glowMaterial.baseColorFactor=Vec4(0,0,0,1);
        glowMaterial.emissiveFactor=Vec3(20,4,0.5f);
        const auto glowHandle=MaterialResource::FromData(glowMaterial);
        for(auto entity:scene.Registry().view<MeshRender>())scene.Registry().get<MeshRender>(entity).material=glowHandle;
        key.Get<Light>().enabled=false;fill.Get<Light>().enabled=false;
        auto& glowFeature=view.SetRenderFeature<RasterFeature>();
        glowFeature.AddProvider<RasterSceneProvider>(store);glowFeature.SetLighting<RasterLightingProvider>(store);
        camera.resolution={128,128};view.SetViewFamily({{camera}});renderer->Update(0.016f);
        composer.Compile();composer.Render();gpu.WaitIdle();const auto withoutGlow=gpu.ReadImage(image);
        AddBloom(glowFeature,BloomSettings{.Intensity=0.5f,.MipCount=5});
        composer.Compile();composer.Render();gpu.WaitIdle();const auto withGlow=gpu.ReadImage(image);
        std::size_t haloPixels=0;
        for(std::size_t y=0;y<256;++y)for(std::size_t x=0;x<256;++x) {
          const auto offset=(y*256+x)*4;
          const bool inside=x<128 && y>=firstRow && y<firstRow+128;
          if(inside && std::to_integer<int>(withoutGlow[offset])<70 &&
             std::to_integer<int>(withGlow[offset])>std::to_integer<int>(withoutGlow[offset])+5)++haloPixels;
          if(!inside)
            for(int channel=0;channel<4;++channel)
              Check(withoutGlow[offset+channel]==withGlow[offset+channel],"Bloom overwrote a neighboring region");
        }
        Check(haloPixels>20,"GPU bloom did not spread highlights beyond emissive geometry");
#ifdef LIGHTHOUSE_TEST_DEFERRED
        const auto hasMotion=[&] {
          for(const auto& resource:composer.GetRenderGraph().GetInspectionData(true).resources)
            if(resource.name=="Deferred Motion")return true;
          return false;
        };
        Check(!hasMotion(),"Unrequested motion attachment was allocated");
        const auto probeImage=gpu.CreateImage({.width=128,.height=128,.format=ImageFormat::RGBA8,.usage={ImageUsage::ColorAttachment,ImageUsage::TransferSource}});
        const auto probeView=gpu.CreateImageView({.image=probeImage,.format=ImageFormat::RGBA8});
        auto& probe=glowFeature.AddDeferredPostProcess<DeferredBufferProbe>(gpu,probeImage,probeView);
        renderer->Update(0.016f);Check(renderer->NeedsRebuild(),"New deferred requirements did not invalidate the graph");
        composer.Compile();Check(hasMotion(),"Requested motion attachment was not allocated");
        composer.Render();gpu.WaitIdle();
        const auto stationary=gpu.ReadImage(probeImage);
        auto channelMax=[](const std::vector<std::byte>& bytes,std::size_t channel){int maximum=0;for(std::size_t i=channel;i<bytes.size();i+=4)maximum=std::max(maximum,std::to_integer<int>(bytes[i]));return maximum;};
        Check(channelMax(stationary,0)==0,"Stationary geometry has nonzero motion");
        Check(channelMax(stationary,1)>200 && channelMax(stationary,2)>200,"Requested metallic/roughness data was not produced");
        camera.cameraPosition.x+=0.15f;camera.view=glm::lookAtRH(camera.cameraPosition,Vec3(0,-0.15f,0),Vec3(0,1,0));
        view.SetViewFamily({{camera}});renderer->Update(0.016f);Check(!renderer->NeedsRebuild(),"Camera motion unnecessarily rebuilt deferred rendering");
        composer.Render();gpu.WaitIdle();Check(channelMax(gpu.ReadImage(probeImage),0)>10,"Motion vectors ignored previous camera matrices");
        composer.Render();gpu.WaitIdle();Check(channelMax(gpu.ReadImage(probeImage),0)==0,"Motion history did not advance after submission");
        for(auto entity:scene.Registry().view<MeshRender,Transform>()) {auto& transform=scene.Registry().get<Transform>(entity);transform.SetPos(transform.GetPos()+Vec3(0.1f,0,0));}
        renderer->Update(0.016f);Check(!renderer->NeedsRebuild(),"Object motion unnecessarily rebuilt deferred rendering");
        composer.Render();gpu.WaitIdle();Check(channelMax(gpu.ReadImage(probeImage),0)>10,"Motion vectors ignored previous object transforms");
        probe.requested=false;renderer->Update(0.016f);Check(renderer->NeedsRebuild(),"Changed deferred dependencies did not invalidate cached graph");
        composer.Compile();Check(!hasMotion(),"Removed motion requirement left an unused attachment");
        composer.Render();gpu.WaitIdle();
#endif
        composer.GetRenderGraph().Clear();composer.RemoveTechnique(ref.handle);
#ifdef LIGHTHOUSE_TEST_DEFERRED
        gpu.DestroyImageView(probeView);gpu.DestroyImage(probeImage);
#endif
      }
      gpu.DestroyImageView(imageView);gpu.DestroyImage(image);
      std::cout<<backend->Name()<<": shared materials (forward/deferred/ray), custom Lit/Unlit pixels, lighting, split regions, cache and GPU mip-chain bloom passed\n";
    }
    backend->Shutdown();return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
