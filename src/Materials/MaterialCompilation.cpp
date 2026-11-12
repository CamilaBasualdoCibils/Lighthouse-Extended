#include <Lighthouse/Extended/Materials/Materials.hpp>
#include <Lighthouse/Engine/Assets/ShaderCompiler.hpp>
#include "ResourceSource.hpp"
#include <algorithm>

namespace Lighthouse
{
CompiledMaterialShaders Material::Compile(MaterialRenderPath path, std::string_view lightingSource, std::string_view indirectSource, std::span<const GBufferAttribute> gbufferLayout, SceneMaterialShaderOptions scene) const
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
  const std::string sceneDeclarations=R"glsl(
layout(push_constant) uniform EESG_DrawIndex { uint index; } EESG_draw;
layout(set=0,binding=0,std140) uniform EESG_View { mat4 viewProjection; mat4 previousViewProjection; vec4 cameraPosition; } EESG_view;
struct EESG_Object { mat4 model; mat4 previousModel; uvec4 material; };
layout(set=0,binding=1,std430) readonly buffer EESG_Objects { EESG_Object values[]; } EESG_objects;
#define EESG_object EESG_objects.values[EESG_draw.index]
struct EESG_FrameData { mat4 viewProjection; mat4 model; mat4 previousViewProjection; mat4 previousModel; vec4 cameraPosition; };
EESG_FrameData EESG_Frame() { return EESG_FrameData(EESG_view.viewProjection,EESG_object.model,EESG_view.previousViewProjection,EESG_object.previousModel,EESG_view.cameraPosition); }
#define frame EESG_Frame()
)glsl";
  const auto removeFrame=[](std::string& text) {
    const auto frame=text.find("uniform FrameData");
    if(frame!=std::string::npos) {
      const auto begin=text.rfind("layout(",frame), end=text.find("} frame;",frame);
      if(begin==std::string::npos || end==std::string::npos) throw std::logic_error("Invalid material frame declaration");
      text.erase(begin,end+8-begin);
    }
  };
  std::string materialSource=source;
  if(scene.enabled) {
    auto declarations=m_declarations;
    declarations.erase(0,declarations.find("struct MaterialParameters"));
    const std::string old="EESG_data.values[", replacement="EESG_parameters.values[EESG_object.material.x + ";
    for(std::size_t pos=0;(pos=declarations.find(old,pos))!=std::string::npos;pos+=replacement.size()) declarations.replace(pos,old.size(),replacement);
    if(scene.bindless) {
      const auto sampler=declarations.find("layout(set=1,binding=");
      const auto function=declarations.find("vec4 SampleTexture2D");
      declarations.erase(sampler==std::string::npos ? function : sampler);
      declarations += "layout(set=1,binding=0) uniform sampler2D EESG_textures["+std::to_string(scene.textureCount)+"];\n";
      declarations += "vec4 SampleTexture2D(uint textureIndex,vec2 uv) { return texture(EESG_textures[nonuniformEXT(EESG_textureIndices.values[EESG_object.material.y+textureIndex])],uv); }\n";
    }
    const auto pos=materialSource.find(m_declarations);
    if(pos==std::string::npos) throw std::logic_error("Missing material declarations");
    materialSource.replace(pos,m_declarations.size(),declarations);
  }
  std::string vertexPrefix;
  if (!gbufferLayout.empty()) {
    vertexPrefix = "#define EESG_DEFERRED_EXTENDED_FRAME\n";
    if (std::find(gbufferLayout.begin(), gbufferLayout.end(), GBufferAttribute::Motion) != gbufferLayout.end())
      vertexPrefix += "#define EESG_GBUFFER_MOTION\n";
  }
  auto vertexText=text(forward?"materials/passes/Shared.forward.vert.glsl":
                                                    "materials/passes/Shared.gbuffer.vert.glsl");
  vertexText.insert(vertexText.find('\n') + 1, vertexPrefix);
  if(scene.enabled) {
    removeFrame(vertexText);
    vertexText.insert(vertexText.find('\n')+1,sceneDeclarations);
  }
  shaders.vertex=ShaderCompiler::Compile(vertexText,{.stage=ShaderStage::Vertex});
  std::string fragment="#version 450\n";
  if(scene.enabled) {
    if(scene.bindless) fragment+="#extension GL_EXT_nonuniform_qualifier : require\n";
    fragment+=sceneDeclarations;
    fragment+="layout(set=0,binding=2,std430) readonly buffer EESG_Parameters { uvec4 values[]; } EESG_parameters;\n";
    fragment+="layout(set=0,binding=3,std430) readonly buffer EESG_TextureIndices { uint values[]; } EESG_textureIndices;\n";
  }
  fragment+=materialSource+"\n#line 1 2\n";
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
  if(scene.enabled) removeFrame(pass);
  fragment+=pass;
  shaders.fragment=ShaderCompiler::Compile(fragment,{.stage=ShaderStage::Fragment});
  return shaders;
}
} // namespace Lighthouse
