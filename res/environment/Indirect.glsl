layout(set=3,binding=0) uniform sampler2D hdriRadiance;
layout(set=3,binding=1) uniform sampler2D hdriIrradiance;
layout(set=3,binding=2) uniform sampler2D hdriBRDF;
vec2 HDRIUV(vec3 direction) {
    vec3 d=normalize(direction);
    return vec2(atan(d.z,d.x)*0.159154943091895+0.5,acos(clamp(d.y,-1.0,1.0))*0.318309886183791);
}
vec3 ForwardDiffuseIrradiance(vec3 position,vec3 normal) {
    return textureLod(hdriIrradiance,HDRIUV(normal),0.0).rgb;
}
vec3 ForwardEnvironmentRadiance(vec3 position,vec3 direction,float roughness) {
    float lod=clamp(roughness,0,1)*float(textureQueryLevels(hdriRadiance)-1);
    return textureLod(hdriRadiance,HDRIUV(direction),lod).rgb;
}
vec2 ForwardEnvironmentBRDF(float nv,float roughness) {
    return textureLod(hdriBRDF,vec2(clamp(nv,0,1),clamp(roughness,0,1)),0).rg;
}
