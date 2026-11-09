vec3 ToLinear(vec3 c) {
 return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), greaterThan(c, vec3(0.04045)));
}
LitSurface evaluateMaterial(SurfaceInput s, MaterialParameters p) {
 vec4 base = SampleTexture2D(p.albedo, s.uv);
 vec4 mr = SampleTexture2D(p.metallicRoughness, s.uv);
 return LitSurface(ToLinear(base.rgb), base.a, mr.b, mr.g,
   SampleTexture2D(p.normalMap, s.uv).xyz * 2.0 - 1.0,
   SampleTexture2D(p.occlusion, s.uv).r, ToLinear(SampleTexture2D(p.emissive, s.uv).rgb));
}
