#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 resultColor;
layout(set=0,binding=0) uniform sampler2D scene;
layout(set=0,binding=2) uniform sampler2D dirtTexture;
layout(set=0,binding=3) uniform sampler2D originalScene;
layout(push_constant) uniform Effect {
    vec4 extentTimeFrame;
    vec4 values0; // intensity, highlight threshold, spread, texture zoom
    vec4 values1; // dirt contrast, dirt saturation, soft glare, scatter gain
} effect;
vec3 highlight(vec2 p) {
    vec3 color=texture(originalScene,p).rgb;
    float luma=dot(color,vec3(0.2126,0.7152,0.0722));
    return color*smoothstep(effect.values0.y,1.0,luma);
}
void main() {
    vec4 source=texture(originalScene,uv);
    if (effect.values0.x<=0.0) { resultColor=source; return; }
    vec2 extent=max(effect.extentTimeFrame.xy,vec2(1.0));
    vec2 assetSize=vec2(textureSize(dirtTexture,0));
    float screenAspect=extent.x/extent.y,assetAspect=assetSize.x/assetSize.y;
    // Center-crop to cover, preserving the supplied texture's aspect ratio.
    // Fixed on the lens: never use camera motion, frame index or time here.
    vec2 fit=vec2(min(1.0,screenAspect/assetAspect),min(1.0,assetAspect/screenAspect));
    vec4 asset=texture(dirtTexture,0.5+(uv-0.5)*fit/max(effect.values0.w,1.0));
    vec3 dirt=pow(max(asset.rgb,vec3(0.0)),vec3(effect.values1.x));
    float luma=dot(dirt,vec3(0.2126,0.7152,0.0722));
    dirt=clamp(mix(vec3(luma),dirt,effect.values1.y),0.0,1.0)*asset.a;
    // The previous passes supply a continuous, separable Gaussian blur. Never
    // sample widely spaced copies of the sharp scene: that stamps ghost lights.
    // Zero spread deliberately retains the exact local-only behavior.
    vec3 glow=effect.values0.z<=0.0 ? highlight(uv):texture(scene,uv).rgb;
    vec3 scatter=clamp(glow*(dirt*effect.values0.x+effect.values1.z)*effect.values1.w,0.0,1.0);
    resultColor=vec4(source.rgb+(1.0-source.rgb)*scatter,source.a);
}
