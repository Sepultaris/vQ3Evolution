#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 resultColor;
layout(set=0,binding=0) uniform sampler2D scene;
layout(push_constant) uniform Effect {
    vec4 extentTimeFrame;
    vec4 values0; // amount, cell size in output pixels, animate
    vec4 values1;
} effect;
uint noiseHash(uint value) {
    value^=value>>16;
    value*=0x7feb352du;
    value^=value>>15;
    value*=0x846ca68bu;
    return value^(value>>16);
}
void main() {
    vec4 source=texture(scene,uv);
    if (effect.values0.x<=0.0) { resultColor=source; return; }
    // Size changes the noise cells, not their amplitude. Output pixel space
    // keeps grain size independent of PT scale, DLSS mode and scene motion.
    uvec2 cell=uvec2(floor(gl_FragCoord.xy/max(effect.values0.y,1.0)));
    uint frame=effect.values0.z>=0.5 ? uint(effect.extentTimeFrame.w) : 0u;
    uint key=cell.x*1597334677u+cell.y*3812015801u+frame*2798796415u+0x85ebca6bu;
    // Integer hashing avoids sin-based precision bands. Change the seed for
    // animation instead of stretching the pattern by multiplying UV by time.
    float noise=float(noiseHash(key)>>8)*(2.0/16777216.0)-1.0;
    resultColor=vec4(clamp(source.rgb+noise*effect.values0.x,0.0,1.0),source.a);
}
