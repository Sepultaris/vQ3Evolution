#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 resultColor;
layout(set=0,binding=0) uniform sampler2D scene;
layout(set=0,binding=3) uniform sampler2D originalScene;
layout(push_constant) uniform Effect {
    vec4 extentTimeFrame;
    vec4 values0;
    vec4 values1;
} effect;
void main() {
    vec4 source=texture(originalScene,uv);
    if (effect.values0.x<=0.0) { resultColor=source; return; }
    vec3 glow=texture(scene,uv).rgb;
    // Preserve the previous bloom composite's bright-core falloff.
    vec3 color=effect.values0.w>=0.5 ? glow*4.0:
        source.rgb+glow*effect.values0.x*(1.0-source.rgb*0.75);
    resultColor=vec4(clamp(color,0.0,1.0),source.a);
}
