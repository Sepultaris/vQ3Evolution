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
    // Cover the whole output footprint, not a few distant sample points.
    // Threshold BEFORE averaging so small bright lights are not lost.
    vec2 stepUV=0.25/max(effect.extentTimeFrame.xy,vec2(1.0));
    vec3 sum=vec3(0.0);
    for (int y=0;y<4;++y) for (int x=0;x<4;++x) {
        vec3 color=texture(scene,uv+(vec2(x,y)-1.5)*stepUV).rgb;
        float t=smoothstep(effect.values0.y,1.0,dot(color,vec3(0.2126,0.7152,0.0722)));
        sum+=color*t;
    }
    resultColor=vec4(sum/16.0,1.0);
}
