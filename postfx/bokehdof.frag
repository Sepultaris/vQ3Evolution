#version 450
// Independent GPL-2.0-or-later implementation. Visual/feature reference:
// Martins Upitis' DoF with bokeh v2.4 (CC BY 3.0), linked by the user at
// github.com/orthecreedence/ghostie/blob/master/opengl/glsl/dof.bokeh.2.4.frag
// This uses artistic focus/defocus units, not that shader's physical lens model.
layout(location=0) in vec2 uv;
layout(location=0) out vec4 resultColor;
layout(set=0,binding=0) uniform sampler2D scene;
layout(set=0,binding=4) uniform sampler2D sceneDepth;
layout(push_constant) uniform Effect {
    vec4 extentTimeFrame;
    vec4 values0; // focus distance, defocus strength, pixel radius, autofocus
    vec4 values1; // highlight threshold/gain, aperture blades, sample count
    vec4 projectionJitter;
} effect;
float viewDepth(vec2 at) {
    float raw=texture(sceneDepth,at+effect.projectionJitter.zw).r;
    // Both Vulkan raster and tracer depth use -P[10] + P[14]/viewDepth.
    // Use the actual projection, not hard-coded camera clipping planes.
    float denominator=raw+effect.projectionJitter.x;
    return abs(denominator)>1e-7 ? clamp(effect.projectionJitter.y/denominator,0.001,1e7):1e7;
}
float circleOfConfusion(float distance,float focus) {
    return clamp((distance-focus)/max(distance,0.001)*effect.values0.y,-1.0,1.0)*effect.values0.z;
}
vec3 highlightColor(vec3 color,float blur) {
    float brightness=dot(color,vec3(0.2126,0.7152,0.0722));
    float excess=max(0.0,brightness-effect.values1.x)/max(0.01,1.0-effect.values1.x);
    return color*(1.0+excess*effect.values1.y*blur);
}
void main() {
    vec4 original=texture(scene,uv);
    float center=viewDepth(uv);
    float focus=effect.values0.w>=0.5 ? viewDepth(vec2(0.5)):effect.values0.x;
    float coc=circleOfConfusion(center,focus),radius=abs(coc);
    if (radius<0.5) { resultColor=original; return; }
    float blend=smoothstep(0.5,1.5,radius);
    float highlightAmount=clamp(radius/max(effect.values0.z,0.001),0.0,1.0);
    vec3 sum=highlightColor(original.rgb,highlightAmount);
    float weights=1.0;
    int count=int(clamp(round(effect.values1.w),16.0,128.0));
    float blades=round(effect.values1.z);
    // Fixed, uniform-area disk samples: no time-dependent noise or frame jitter.
    for (int i=0;i<128;++i) {
        if (i>=count) break;
        float angle=float(i)*2.39996323;
        float radial=sqrt((float(i)+0.5)/float(count));
        if (blades>=3.0) {
            float sector=6.28318530718/blades;
            radial*=cos(3.14159265359/blades)/cos(mod(angle+0.5*sector,sector)-0.5*sector);
        }
        vec2 offset=vec2(cos(angle),sin(angle))*radial*radius;
        vec2 at=clamp(uv+offset/effect.extentTimeFrame.xy,vec2(0.0),vec2(1.0));
        float other=viewDepth(at),weight=1.0;
        // An in-focus foreground surface must not leak into a blurred background.
        // Allow a nearer sample only where its own blur disk covers this pixel.
        if (other<center-max(1.0,center*0.01)) {
            float otherRadius=abs(circleOfConfusion(other,focus));
            weight=smoothstep(-0.5,0.5,otherRadius-length(offset));
        }
        sum+=highlightColor(texture(scene,at).rgb,highlightAmount)*weight;
        weights+=weight;
    }
    resultColor=vec4(mix(original.rgb,clamp(sum/weights,0.0,1.0),blend),original.a);
}
