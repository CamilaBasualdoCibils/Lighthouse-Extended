layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 worldNormal;
layout(location=2) in vec2 surfaceUV;
layout(location=0) out vec4 color;
layout(location=1) out vec4 normalAttribute;
layout(location=2) out vec4 positionAttribute;
layout(location=3) out vec4 propertiesAttribute;
layout(location=4) out vec4 emissionAttribute;

void main() {
    LitSurface surface = evaluateMaterial(SurfaceInput(worldPosition, worldNormal, surfaceUV), EESG_LoadParameters());
    if (surface.opacity <= 0.0) discard;
    color = vec4(max(surface.color, vec3(0.0)), surface.opacity);
    normalAttribute = vec4(MappedNormal(surface.tangentNormal), 0.0);
    positionAttribute = vec4(worldPosition, 1.0);
    propertiesAttribute = vec4(clamp(surface.metallic, 0.0, 1.0),
                              clamp(surface.roughness, 0.045, 1.0),
                              clamp(surface.occlusion, 0.0, 1.0), 0.0);
    emissionAttribute = vec4(max(surface.emission, vec3(0.0)), 0.0);
}
