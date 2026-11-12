UnlitSurface evaluateMaterial(SurfaceInput surface, MaterialParameters parameters)
{
    return UnlitSurface(SampleTexture2D(parameters.color, surface.uv).rgb * parameters.tint.rgb, 1.0);
}
