#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 resultColor;
layout(set=0,binding=0) uniform sampler2D scene;
layout(push_constant) uniform Effect {
    vec4 extentTimeFrame;
    vec4 values0; // separation in output pixels, radial falloff
    vec4 values1;
} effect;
void main() {
    vec4 source=texture(scene,uv);
    if (effect.values0.x<=0.0) { resultColor=source; return; }
    vec2 extent=max(effect.extentTimeFrame.xy,vec2(1.0));
    vec2 position=(uv-0.5)*extent;
    float distanceToCenter=length(position);
    float radius=clamp(distanceToCenter/max(length(0.5*extent),1.0),0.0,1.0);
    vec2 direction=position/max(distanceToCenter,0.0001);
    // Symmetric offsets keep green, alpha and the optical center stationary.
    // Divide by OUTPUT extent: strength does not depend on PT render scale.
    vec2 offset=direction*effect.values0.x*pow(radius,effect.values0.y)/extent;
    resultColor=vec4(texture(scene,uv+offset).r,source.g,
                     texture(scene,uv-offset).b,source.a);
}
