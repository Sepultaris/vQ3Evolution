#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec2 uv;
layout(location=0) out vec4 resultColor;
layout(set=0,binding=0) uniform sampler2D scene;
#include "motionblur_guides.glsl"
void main() {
    vec4 original=texture(scene,uv);
    if (effect.motionInfo.y<.5 || effect.values0.x<=0 || effect.values0.y<=0 || effect.values0.z<=0) {
        resultColor=original; return;
    }
    float raw=rawDepth(uv),depth=linearDepth(raw);
    vec2 velocity=shutterVector(uv,raw);
    float distance=length(velocity);
    if (distance<=max(effect.values1.y,.001)) { resultColor=original; return; }
    // Adaptive tap count, capped by the user's quality setting. Symmetric
    // shutter integration avoids directional image lag; no previous-color trails.
    int count=int(clamp(ceil(distance*1.5),8.0,effect.values0.w));
    vec3 sum=original.rgb;
    float total=1.0;
    for (int i=0;i<64;++i) {
        if (i>=count) break;
        float t=(float(i)+.5)/float(count)-.5;
        vec2 at=uv+velocity*t/effect.extentTimeFrame.xy;
        if (any(lessThan(at,vec2(0))) || any(greaterThan(at,vec2(1)))) continue;
        float otherRaw=rawDepth(at),other=linearDepth(otherRaw);
        float relative=abs(other-depth)/max(min(other,depth),1.0);
        float weight=1.0-smoothstep(effect.values1.x,effect.values1.x*2.0,relative);
        // Keep static foreground/weapon silhouettes separate from moving scenery.
        vec2 otherVelocity=shutterVector(at,otherRaw);
        weight*=1.0-smoothstep(2.0,8.0,length(otherVelocity-velocity));
        sum+=texture(scene,at).rgb*weight; total+=weight;
    }
    float fade=smoothstep(effect.values1.y,effect.values1.y+1.0,distance);
    resultColor=vec4(mix(original.rgb,sum/total,fade),original.a);
}
