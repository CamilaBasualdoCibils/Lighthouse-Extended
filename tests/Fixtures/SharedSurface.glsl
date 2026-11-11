LitSurface evaluateMaterial(SurfaceInput surface,MaterialParameters parameters) {
    return LitSurface(parameters.color.rgb,parameters.color.a,0.0,0.6,vec3(0,0,1),1.0,parameters.emission);
}
