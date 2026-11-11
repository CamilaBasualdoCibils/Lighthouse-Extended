#version 450
layout(set=0,binding=0,std140) uniform Settings { vec4 value; } settings;
layout(location=0) out vec4 color;
void main() {
    const float pi=3.141592653589793;
    vec2 uv=gl_FragCoord.xy/settings.value.xy;
    float nv=max(uv.x,1e-4),roughness=uv.y,a=roughness*roughness,a2=a*a;
    vec3 v=vec3(sqrt(max(0,1-nv*nv)),0,nv);
    vec2 result=vec2(0);
    uint count=uint(settings.value.z);
    for(uint i=0u;i<count;++i) {
        vec2 xi=vec2((float(i)+0.5)/float(count),float(bitfieldReverse(i))*2.3283064365386963e-10);
        float phi=2*pi*xi.x,ct=sqrt((1-xi.y)/(1+(a2-1)*xi.y)),st=sqrt(max(0,1-ct*ct));
        vec3 h=vec3(cos(phi)*st,sin(phi)*st,ct);
        float vh=max(dot(v,h),0);
        vec3 l=2*vh*h-v;
        float nl=max(l.z,0);
        if(nl>0) {
            float masking=nl*sqrt(nv*nv*(1-a2)+a2)+nv*sqrt(nl*nl*(1-a2)+a2);
            float weight=2*nl*vh/max(masking*ct,1e-6);
            float fc=pow(1-vh,5);
            result+=vec2(1-fc,fc)*weight;
        }
    }
    color=vec4(result/float(count),0,1);
}
