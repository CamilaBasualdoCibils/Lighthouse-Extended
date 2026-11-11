layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 worldNormal;
layout(location=2) in vec2 surfaceUV;
layout(location=0) out vec4 color;
layout(set=0,binding=0,std140) uniform FrameData {
    mat4 viewProjection;
    mat4 model;
    vec4 cameraPosition;
} frame;
void main() {
    LitSurface surface=evaluateMaterial(SurfaceInput(worldPosition,worldNormal,surfaceUV),EESG_LoadParameters());
    if(surface.opacity<=0.0) discard;
    LitShadingSurface shading=LitShadingSurface(max(surface.color,vec3(0)),MappedNormal(surface.tangentNormal),
                                               clamp(surface.metallic,0,1),clamp(surface.roughness,0.045,1));
    vec3 radiance=max(surface.emission,vec3(0))+shading.albedo*ForwardAmbientRadiance()*clamp(surface.occlusion,0,1);
    vec3 viewDirection=normalize(frame.cameraPosition.xyz-worldPosition);
    for(uint i=0u;i<ForwardLightCount();++i)
        radiance+=EvaluateSurface(shading,viewDirection,ForwardLightAt(i,worldPosition));
    color=vec4(radiance,surface.opacity);
}
