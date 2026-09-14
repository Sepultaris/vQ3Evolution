#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 resultColor;
layout(set=0,binding=0) uniform sampler2D scene;
layout(push_constant) uniform Effect {
    vec4 extentTimeFrame;
    vec4 values0;
    vec4 values1;
} effect;
void main() {
    vec4 color=texture(scene,uv);
    vec2 distanceFromCenter=(uv-0.5)*2.0;
    float vignette=smoothstep(0.15,1.6,dot(distanceFromCenter,distanceFromCenter));
    resultColor=vec4(color.rgb*(1.0-vignette*effect.values0.x),color.a);
}
