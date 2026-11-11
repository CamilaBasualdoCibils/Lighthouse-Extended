#version 450
layout(set=0,binding=0,std140) uniform Settings { vec4 value; } settings;
layout(set=0,binding=1) uniform sampler2D radiance;
layout(location=0) out vec4 color;
const float pi=3.141592653589793;
vec2 UV(vec3 d) { return vec2(atan(d.z,d.x)/(2*pi)+0.5,acos(clamp(d.y,-1,1))/pi); }
void main() {
    vec2 uv=gl_FragCoord.xy/settings.value.xy;
    float phi=(uv.x-0.5)*2*pi,theta=uv.y*pi;
    vec3 n=vec3(cos(phi)*sin(theta),cos(theta),sin(phi)*sin(theta));
    float roughness=settings.value.w;
    if(roughness==0.0) { color=vec4(textureLod(radiance,uv,0).rgb,1); return; }
    vec3 up=abs(n.y)<0.999?vec3(0,1,0):vec3(1,0,0);
    vec3 tangent=normalize(cross(up,n)),bitangent=cross(n,tangent);
    float a=roughness*roughness,a2=a*a,total=0;
    vec3 sum=vec3(0);
    uint count=uint(settings.value.z);
    vec2 sourceSize=vec2(textureSize(radiance,0));
    for(uint i=0u;i<count;++i) {
        vec2 xi=vec2((float(i)+0.5)/float(count),float(bitfieldReverse(i))*2.3283064365386963e-10);
        float p=2*pi*xi.x,ct=sqrt((1-xi.y)/(1+(a2-1)*xi.y)),st=sqrt(max(0,1-ct*ct));
        vec3 h=normalize(tangent*cos(p)*st+bitangent*sin(p)*st+n*ct);
        vec3 l=normalize(2*dot(n,h)*h-n);
        float nl=max(dot(n,l),0);
        if(nl>0) {
            float denominator=ct*ct*(a2-1)+1;
            float distribution=a2/max(pi*denominator*denominator,1e-10);
            float pdf=max(distribution*0.25,1e-8); // V=N, so N.H == V.H.
            float sampleAngle=1/(float(count)*pdf);
            float texelAngle=2*pi*pi*max(sqrt(max(0,1-l.y*l.y)),1e-4)/(sourceSize.x*sourceSize.y);
            float lod=clamp(0.5*log2(sampleAngle/texelAngle),0,float(textureQueryLevels(radiance)-1));
            sum+=textureLod(radiance,UV(l),lod).rgb*nl;total+=nl;
        }
    }
    color=vec4(sum/max(total,1e-6),1);
}
