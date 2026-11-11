#include <Lighthouse/Extended/Materials/Materials.hpp>
#include <Lighthouse/Engine/Assets/ShaderCompiler.hpp>
#include "ResourceSource.hpp"

namespace Lighthouse
{
CompiledMaterialShaders Material::Compile(MaterialRenderPath path, std::string_view lightingSource) const
{
  const std::string base=model==SurfaceModel::Lit?"Lit":"Unlit";
  const auto text=[](const std::string& name){return Detail::ResourceText(Detail::EmbeddedPath(name));};
  CompiledMaterialShaders shaders;
  if(path==MaterialRenderPath::RayTracing)
  {
    shaders.closestHit=ShaderCompiler::Compile(
      "#version 460\n#extension GL_EXT_ray_tracing : require\n#define EESG_RAY_TRACING\n"+
      source+"\n#line 1 2\n"+text("materials/passes/"+base+".rchit.glsl"),{.stage=ShaderStage::ClosestHit});
    return shaders;
  }
  const bool forward=path==MaterialRenderPath::Forward;
  shaders.vertex=ShaderCompiler::Compile(text(forward?"materials/passes/Shared.forward.vert.glsl":
                                                    "materials/passes/Shared.gbuffer.vert.glsl"),{.stage=ShaderStage::Vertex});
  std::string fragment="#version 450\n"+source+"\n#line 1 2\n";
  if(forward && model==SurfaceModel::Lit)
  {
    fragment+=lightingSource.empty()?R"glsl(
struct LightSample { vec3 direction; vec3 radiance; };
uint ForwardLightCount(){return 0u;}
LightSample ForwardLightAt(uint index,vec3 position){return LightSample(vec3(0),vec3(0));}
vec3 ForwardAmbientRadiance(){return vec3(0.03);}
)glsl":std::string(lightingSource);
    fragment+=text("materials/passes/Lit.BRDF.glsl");
  }
  auto pass=text("materials/passes/"+base+(forward?".forward.frag.glsl":".gbuffer.frag.glsl"));
  if(model==SurfaceModel::Lit) pass.insert(pass.find("void main()"), text("materials/passes/Lit.normal.glsl"));
  fragment+=pass;
  shaders.fragment=ShaderCompiler::Compile(fragment,{.stage=ShaderStage::Fragment});
  return shaders;
}
} // namespace Lighthouse
