vec3 MappedNormal(vec3 tangentNormal) {
    vec3 n = normalize(worldNormal);
    vec3 dx = dFdx(worldPosition), dy = dFdy(worldPosition);
    vec2 ux = dFdx(surfaceUV), uy = dFdy(surfaceUV);
    float determinant = ux.x * uy.y - ux.y * uy.x;
    if (abs(determinant) < 1e-8 || dot(tangentNormal, tangentNormal) < 1e-8)
        return n;
    vec3 tangent = (dx * uy.y - dy * ux.y) / determinant;
    vec3 bitangent = (dy * ux.x - dx * uy.x) / determinant;
    tangent -= n * dot(n, tangent);
    if (dot(tangent, tangent) < 1e-8) return n;
    tangent = normalize(tangent);
    float handedness = dot(cross(n, tangent), bitangent) < 0.0 ? -1.0 : 1.0;
    bitangent = cross(n, tangent) * handedness;
    return normalize(mat3(tangent, bitangent, n) * normalize(tangentNormal));
}
