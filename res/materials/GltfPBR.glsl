vec3 GltfToLinear(vec3 value) {
    return mix(value/12.92,pow((value+0.055)/1.055,vec3(2.4)),greaterThan(value,vec3(0.04045)));
}
LitSurface evaluateMaterial(SurfaceInput surface,MaterialParameters p) {
    vec4 base=SampleTexture2D(p.baseColor,surface.uv);
    base=vec4(GltfToLinear(base.rgb),base.a)*p.baseColorFactor;
    vec4 mr=SampleTexture2D(p.metallicRoughness,surface.uv);
    vec3 normal=p.hasNormal?SampleTexture2D(p.normal,surface.uv).xyz*2.0-1.0:vec3(0,0,1);
    normal.xy*=p.normalScale;
#ifndef EESG_RAY_TRACING
    if(p.doubleSided && !gl_FrontFacing) normal=-normal;
#endif
    float alpha=p.alphaMode==1 && base.a<p.alphaCutoff?0.0:(p.alphaMode==2?base.a:1.0);
    return LitSurface(base.rgb,alpha,mr.b*p.metallicFactor,mr.g*p.roughnessFactor,normal,
      mix(1.0,SampleTexture2D(p.occlusion,surface.uv).r,p.occlusionStrength),
      GltfToLinear(SampleTexture2D(p.emissive,surface.uv).rgb)*p.emissiveFactor);
}
