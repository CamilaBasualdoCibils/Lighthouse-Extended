layout(set=0,binding=0) uniform sampler2D albedoBuffer;
layout(set=0,binding=1) uniform sampler2D normalBuffer;
layout(set=0,binding=2) uniform sampler2D positionBuffer;
layout(set=0,binding=3) uniform sampler2D propertiesBuffer;
layout(set=0,binding=4) uniform sampler2D emissionBuffer;
layout(set=0,binding=8) uniform sampler2D depthBuffer;
layout(location=0) out vec4 color;
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    if(texelFetch(depthBuffer,pixel,0).r>=1.0) discard; // Preserve sky/background.
    vec4 albedo=texelFetch(albedoBuffer,pixel,0);
    vec4 properties=texelFetch(propertiesBuffer,pixel,0);
    if(properties.w>0.5) { color=albedo; return; } // Unlit materials.
    vec3 position=texelFetch(positionBuffer,pixel,0).xyz;
    vec3 n=normalize(texelFetch(normalBuffer,pixel,0).xyz);
    vec3 v=normalize(sceneLighting.cameraPosition.xyz-position);
    LitShadingSurface surface=LitShadingSurface(albedo.rgb,n,properties.x,properties.y);
    vec3 radiance=texelFetch(emissionBuffer,pixel,0).rgb;
    radiance+=albedo.rgb*DeferredAmbientRadiance()*properties.z;
    radiance+=EvaluateIndirectSurface(surface,position,v)*properties.z;
    for(uint i=0u;i<DeferredLightCount();++i)
        radiance+=EvaluateSurface(surface,v,DeferredLightAt(i,position));
    color=vec4(radiance,albedo.a);
}
