#version 450
layout(set=0,binding=0) uniform sampler2D sourceImage;
layout(set=0,binding=1) uniform sampler2D bloomImage;
layout(set=0,binding=2,std140) uniform BloomParameters {
    vec4 settings; // threshold, soft knee, intensity, operation
    vec4 sourceRegion; // UV scale, UV bias
    vec4 outputSize; // reciprocal output dimensions
};
layout(location=0) out vec4 color;
vec3 source(vec2 uv) {
    vec2 texel=1.0/vec2(textureSize(sourceImage,0));
    vec2 lo=sourceRegion.zw+0.5*texel;
    vec2 hi=sourceRegion.zw+sourceRegion.xy-0.5*texel;
    return texture(sourceImage,clamp(uv*sourceRegion.xy+sourceRegion.zw,lo,hi)).rgb;
}
vec3 highlight(vec3 value) {
    float brightness=max(max(value.r,value.g),value.b);
    float knee=settings.x*settings.y;
    float soft=clamp(brightness-settings.x+knee,0.0,2.0*knee);
    soft=soft*soft/(4.0*knee+0.00001);
    return value*(max(brightness-settings.x,soft)/max(brightness,0.00001));
}
vec3 downsample(vec2 uv, bool prefilter) {
    vec2 t=1.0/(vec2(textureSize(sourceImage,0))*sourceRegion.xy);
    const vec2 offsets[13]=vec2[13](
        vec2(-2,2),vec2(0,2),vec2(2,2),
        vec2(-2,0),vec2(0,0),vec2(2,0),
        vec2(-2,-2),vec2(0,-2),vec2(2,-2),
        vec2(-1,1),vec2(1,1),vec2(-1,-1),vec2(1,-1));
    const float kernel[13]=float[13](
        0.03125,0.0625,0.03125,0.0625,0.125,0.0625,
        0.03125,0.0625,0.03125,0.125,0.125,0.125,0.125);
    vec3 taps[13];
    vec3 sum=vec3(0);
    for(int i=0;i<13;++i) {
        vec3 value=source(uv+offsets[i]*t);
        // Keep non-finite scene values out of the entire bloom pyramid.
        value=mix(value,vec3(0),isnan(value));
        taps[i]=clamp(value,vec3(0),vec3(65504));
        sum+=taps[i]*kernel[i];
    }
    if(!prefilter) return sum;
    // Average overlapping boxes BEFORE computing nonlinear weights. Applying
    // weights independently to bilinear taps makes a highlight change energy
    // when it straddles two input pixels instead of one.
    vec3 groups[5]=vec3[5](
        (taps[9]+taps[10]+taps[11]+taps[12])*0.25,
        (taps[0]+taps[1]+taps[3]+taps[4])*0.25,
        (taps[1]+taps[2]+taps[4]+taps[5])*0.25,
        (taps[3]+taps[4]+taps[6]+taps[7])*0.25,
        (taps[4]+taps[5]+taps[7]+taps[8])*0.25);
    // A shared spike can land in every overlapping box. Use the median of
    // the original footprint as a robust neighborhood reference, so that such
    // an outlier cannot defeat normalized weights by appearing in every group.
    float luminances[13];
    for(int i=0;i<13;++i) luminances[i]=dot(taps[i],vec3(0.2126,0.7152,0.0722));
    for(int i=1;i<13;++i) for(int j=i;j>0;--j) {
        float lo=min(luminances[j-1],luminances[j]);
        float hi=max(luminances[j-1],luminances[j]);
        luminances[j-1]=lo;
        luminances[j]=hi;
    }
    // Keep rejection independent of Threshold: raising the threshold must
    // never increase an isolated highlight's bloom contribution.
    float localLimit=max(2.0,4.0*luminances[6]);
    sum=vec3(0);
    float weightSum=0;
    for(int i=0;i<5;++i) {
        float luminance=dot(groups[i],vec3(0.2126,0.7152,0.0722));
        groups[i]*=min(1.0,localLimit/max(luminance,0.00001));
        // Normalized Karis weights suppress isolated HDR outliers, while a
        // uniformly bright surface retains its original highlight radiance.
        float weight=(i==0 ? 0.5 : 0.125)/(1.0+dot(groups[i],vec3(0.2126,0.7152,0.0722)));
        sum+=highlight(groups[i])*weight;
        weightSum+=weight;
    }
    return sum/max(weightSum,0.000001);
}
vec3 upsample(vec2 uv) {
    vec2 t=1.0/vec2(textureSize(bloomImage,0));
    vec3 sum=vec3(0);
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x)
        sum+=texture(bloomImage,uv+vec2(x,y)*t).rgb*float((x==0?2:1)*(y==0?2:1));
    return sum/16.0;
}
void main() {
    vec2 uv=gl_FragCoord.xy*outputSize.xy;
    int operation=int(settings.w);
    if(operation<=1) {
        vec3 value=downsample(uv,operation==0);
        color=vec4(value,1);
    } else if(operation==2) {
        // Normalize reconstruction so intensity does not grow with mip count.
        color=vec4(0.5*(source(uv)+upsample(uv)),1);
    } else {
        vec2 at=uv*sourceRegion.xy+sourceRegion.zw;
        color=vec4(source(uv)+settings.z*upsample(uv),texture(sourceImage,at).a);
    }
}
