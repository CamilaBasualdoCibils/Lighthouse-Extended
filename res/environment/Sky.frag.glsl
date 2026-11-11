#version 450
layout(set=0,binding=0) uniform sampler2D hdri;
layout(set=0,binding=1,std140) uniform SkyFrame { mat4 inverseProjection; mat4 inverseViewRotation; } sky;
layout(location=0) in vec2 ndc;
layout(location=0) out vec4 color;
void main() {
    // Inverse projection transforms interpolated fullscreen NDC from the vertex shader.
    vec4 eye=sky.inverseProjection*vec4(ndc,1,1);
    vec3 d=normalize(mat3(sky.inverseViewRotation)*eye.xyz);
    vec2 uv=vec2(atan(d.z,d.x)*0.159154943091895+0.5,acos(clamp(d.y,-1.0,1.0))*0.318309886183791);
    color=vec4(textureLod(hdri,uv,0).rgb,1);
}
