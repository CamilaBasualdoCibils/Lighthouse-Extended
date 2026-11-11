// Split-sum IBL: providers supply GGX-filtered incident radiance and a DFG LUT;
// the material retains albedo, Fresnel, metallic and diffuse/specular response.
vec3 EvaluateIndirectSurface(LitShadingSurface s, vec3 position, vec3 v) {
    const float pi=3.141592653589793;
    float nv=max(dot(s.normal,v),1e-4);
    vec3 f0=mix(vec3(0.04),s.albedo,s.metallic);
    vec3 fresnel=f0+(max(vec3(1.0-s.roughness),f0)-f0)*pow(1.0-nv,5.0);
    vec3 diffuse=(1.0-fresnel)*(1.0-s.metallic)*s.albedo*
                  ForwardDiffuseIrradiance(position,s.normal)/pi;
    vec3 reflected=reflect(-v,s.normal);
    vec3 prefiltered=ForwardEnvironmentRadiance(position,reflected,s.roughness);
    vec2 brdf=ForwardEnvironmentBRDF(nv,s.roughness);
    return diffuse+prefiltered*(f0*brdf.x+brdf.y);
}
