#version 450
layout(location=0) out vec2 ndc;
void main() {
    vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    ndc=p[gl_VertexIndex];
    gl_Position=vec4(ndc,0,1);
}
