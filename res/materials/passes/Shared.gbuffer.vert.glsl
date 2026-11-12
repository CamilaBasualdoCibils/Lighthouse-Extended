#version 450
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec2 surfaceUV;
layout(set = 0, binding = 0, std140) uniform FrameData
{
    mat4 viewProjection;
    mat4 model;
#ifdef EESG_DEFERRED_EXTENDED_FRAME
    mat4 previousViewProjection;
    mat4 previousModel;
#endif
} frame;

#ifdef EESG_GBUFFER_MOTION
layout(location=3) out vec4 currentClip;
layout(location=4) out vec4 previousClip;
#endif
void main()
{
    worldPosition = (frame.model * vec4(position, 1.0)).xyz;
    worldNormal = transpose(inverse(mat3(frame.model))) * normal;
    surfaceUV = uv;
    gl_Position = frame.viewProjection * vec4(worldPosition, 1.0);
#ifdef EESG_GBUFFER_MOTION
    currentClip = gl_Position;
    previousClip = frame.previousViewProjection * frame.previousModel * vec4(position,1.0);
#endif
}
