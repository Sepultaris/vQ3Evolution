#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 resultColor;
layout(set=0,binding=0) uniform sampler2D scene;
layout(push_constant) uniform Effect {
    vec4 extentTimeFrame;
    vec4 values0; // intensity, threshold, radius, glow-only
    vec4 values1;
} effect;
void main() {
    vec2 stepUV=0.25/max(effect.extentTimeFrame.xy,vec2(1.0));
    float threshold=clamp(effect.values0.y,0.0,0.98);
    vec3 sum=vec3(0.0);
    // Keep the former bloom's 2x2 average / squared highlight extraction.
    // Combine four such cells into the shared quarter-resolution float target.
    // Normalized sampling also supports a lower-resolution native tracer input;
    // the chain's clamp sampler handles odd-sized scene edges.
    for (int y=0;y<4;y+=2) for (int x=0;x<4;x+=2) {
        vec3 color=vec3(0.0);
        for (int j=0;j<2;++j) for (int i=0;i<2;++i)
            color+=texture(scene,uv+(vec2(x+i,y+j)-1.5)*stepUV).rgb;
        color*=0.25;
        float w=clamp((dot(color,vec3(0.299,0.587,0.114))-threshold)/(1.0-threshold),0.0,1.0);
        sum+=color*w*w;
    }
    resultColor=vec4(sum*0.25,1.0);
}
