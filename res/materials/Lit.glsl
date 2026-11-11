// Shared linear metallic/roughness surface contract for forward, deferred and ray targets.
struct SurfaceInput { vec3 position; vec3 normal; vec2 uv; };
struct LitSurface {
    vec3 color;
    float opacity;
    float metallic;
    float roughness;
    vec3 tangentNormal;
    float occlusion;
    vec3 emission;
};
