#include <Lighthouse/Extended/Materials/Materials.hpp>
#include <Lighthouse/Engine/Assets/ShaderCompiler.hpp>
#include "ResourceSource.hpp"
#include <algorithm>

namespace Lighthouse
{
CompiledMaterialShaders Material::Compile(MaterialRenderPath path, std::string_view lightingSource, std::string_view indirectSource, std::span<const GBufferAttribute> gbufferLayout) const
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
  std::string vertexPrefix;
  if (!gbufferLayout.empty()) {
    vertexPrefix = "#define EESG_DEFERRED_EXTENDED_FRAME\n";
    if (std::find(gbufferLayout.begin(), gbufferLayout.end(), GBufferAttribute::Motion) != gbufferLayout.end())
      vertexPrefix += "#define EESG_GBUFFER_MOTION\n";
  }
  auto vertexText=text(forward?"materials/passes/Shared.forward.vert.glsl":
                                                    "materials/passes/Shared.gbuffer.vert.glsl");
  vertexText.insert(vertexText.find('\n') + 1, vertexPrefix);
  shaders.vertex=ShaderCompiler::Compile(vertexText,{.stage=ShaderStage::Vertex});
  std::string fragment="#version 450\n"+source+"\n#line 1 2\n";
  if(forward && model==SurfaceModel::Lit)
  {
    fragment+=lightingSource.empty()?R"glsl(
struct LightSample { vec3 direction; vec3 radiance; };
uint ForwardLightCount(){return 0u;}
LightSample ForwardLightAt(uint index,vec3 position){return LightSample(vec3(0),vec3(0));}
vec3 ForwardAmbientRadiance(){return vec3(0);}
)glsl":std::string(lightingSource);
    fragment+=text("materials/passes/Lit.BRDF.glsl");
    fragment+=indirectSource.empty() ? R"glsl(
vec3 ForwardDiffuseIrradiance(vec3 position, vec3 normal) { return vec3(0); }
vec3 ForwardEnvironmentRadiance(vec3 position, vec3 direction, float roughness) { return vec3(0); }
vec2 ForwardEnvironmentBRDF(float nv, float roughness) { return vec2(0); }
)glsl" : std::string(indirectSource);
    fragment+=text("materials/passes/Lit.IBL.glsl");
  }
  auto pass=text("materials/passes/"+base+(forward?".forward.frag.glsl":".gbuffer.frag.glsl"));
  if (!forward && !gbufferLayout.empty()) {
    pass = "layout(location=0) in vec3 worldPosition;\nlayout(location=1) in vec3 worldNormal;\nlayout(location=2) in vec2 surfaceUV;\n";
    const bool motion = std::find(gbufferLayout.begin(), gbufferLayout.end(), GBufferAttribute::Motion) != gbufferLayout.end();
    if(motion) pass += "layout(location=3) in vec4 currentClip;\nlayout(location=4) in vec4 previousClip;\n";
    for(std::size_t i=0;i<gbufferLayout.size();++i)
      pass += "layout(location="+std::to_string(i)+") out vec4 attribute"+std::to_string(i)+";\n";
    pass += "void main() {\n";
    pass += base+"Surface surface=evaluateMaterial(SurfaceInput(worldPosition,worldNormal,surfaceUV),EESG_LoadParameters());\nif(surface.opacity<=0.0)discard;\n";
    const bool lit=model==SurfaceModel::Lit;
    for(std::size_t i=0;i<gbufferLayout.size();++i) {
      std::string value;
      switch(gbufferLayout[i]) {
      case GBufferAttribute::Color: value="vec4(max(surface.color,vec3(0)),surface.opacity)";break;
      case GBufferAttribute::WorldNormal: value=lit?"vec4(MappedNormal(surface.tangentNormal),0)":"vec4(normalize(worldNormal),0)";break;
      case GBufferAttribute::WorldPosition: value="vec4(worldPosition,1)";break;
      case GBufferAttribute::SurfaceProperties: value=lit?"vec4(clamp(surface.metallic,0,1),clamp(surface.roughness,0.045,1),clamp(surface.occlusion,0,1),0)":"vec4(0,1,1,1)";break;
      case GBufferAttribute::Emission: value=lit?"vec4(max(surface.emission,vec3(0)),0)":"vec4(0)";break;
      case GBufferAttribute::Metallic: value=lit?"vec4(clamp(surface.metallic,0,1),0,0,0)":"vec4(0)";break;
      case GBufferAttribute::Roughness: value=lit?"vec4(clamp(surface.roughness,0.045,1),0,0,0)":"vec4(1,0,0,0)";break;
      case GBufferAttribute::Motion: value="vec4((currentClip.w>0.000001 && previousClip.w>0.000001)?0.5*(currentClip.xy/currentClip.w-previousClip.xy/previousClip.w):vec2(0),0,0)";break;
      }
      pass += "attribute"+std::to_string(i)+"="+value+";\n";
    }
    pass += "}\n";
  }
  if(model==SurfaceModel::Lit) pass.insert(pass.find("void main()"), text("materials/passes/Lit.normal.glsl"));
  fragment+=pass;
  shaders.fragment=ShaderCompiler::Compile(fragment,{.stage=ShaderStage::Fragment});
  return shaders;
}
} // namespace Lighthouse
