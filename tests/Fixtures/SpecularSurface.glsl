LitSurface evaluateMaterial(SurfaceInput surface, MaterialParameters parameters) {
    return LitSurface(vec3(0.9),1.0,1.0,parameters.roughness,vec3(0,0,1),1.0,vec3(0));
}
