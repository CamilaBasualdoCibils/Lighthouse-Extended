#version 450
layout(set=0,binding=0) uniform sampler2D sceneColor;
layout(location=0) out vec4 color;
void main() {
    vec4 value=texelFetch(sceneColor,ivec2(gl_FragCoord.xy),0);
    // Reinhard display mapping followed by exact sRGB encoding.
    vec3 linear=max(value.rgb,vec3(0));
    linear=linear/(1.0+linear);
    vec3 srgb=mix(12.92*linear,1.055*pow(linear,vec3(1.0/2.4))-0.055,
                  greaterThan(linear,vec3(0.0031308)));
    color=vec4(srgb,value.a);
}
