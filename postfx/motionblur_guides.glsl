// v5 guide contract, GPL-2.0-or-later. No history-color accumulation.
layout(set=0,binding=4) uniform sampler2D sceneDepth;
layout(set=0,binding=5) uniform sampler2D sceneMotion;
layout(push_constant) uniform Effect {
    vec4 extentTimeFrame;
    vec4 values0; // strength, shutter angle, max streak pixels, sample limit
    vec4 values1; // relative depth tolerance, minimum motion, reference FPS, unused
    vec4 projectionJitter;
    vec4 motionInfo; // traced vectors, valid, delta seconds, reconstructed color flag
    vec4 previousClipX;
    vec4 previousClipY;
    vec4 previousClipW;
} effect;
vec2 guideUV(vec2 at) { return at+effect.projectionJitter.zw*effect.motionInfo.w; }
float rawDepth(vec2 at) { return texture(sceneDepth,guideUV(at)).r; }
float linearDepth(float raw) {
    float d=raw+effect.projectionJitter.x;
    return abs(d)>1e-7 ? clamp(effect.projectionJitter.y/d,.001,1e7):1e7;
}
vec2 motionVector(vec2 at,float raw) {
    if (effect.motionInfo.y<.5) return vec2(0);
    if (effect.motionInfo.x>.5)
        return texture(sceneMotion,guideUV(at)).rg;
    // Raw raster color is jittered only when reconstruction did not evaluate.
    // The CPU supplies zero jitter in this path when temporal sampling is off.
    vec2 cleanUV=at-(1.0-effect.motionInfo.w)*effect.projectionJitter.zw;
    vec4 clip=vec4(cleanUV*2.0-1.0,raw,1.0);
    vec3 old=vec3(dot(effect.previousClipX,clip),dot(effect.previousClipY,clip),dot(effect.previousClipW,clip));
    return old.z>1e-5 ? old.xy/old.z*.5+.5-cleanUV:vec2(0);
}
vec2 shutterVector(vec2 at,float raw) {
    float scale=effect.values0.x*effect.values0.y/360.0;
    if (effect.values1.z>0.0)
        scale*=clamp(1.0/max(effect.values1.z*effect.motionInfo.z,.001),0.0,8.0);
    vec2 velocity=motionVector(at,raw)*effect.extentTimeFrame.xy*scale;
    // Reject bad inputs; do not let undefined guides spread NaNs across color.
    if (any(isnan(velocity)) || any(isinf(velocity))) return vec2(0);
    float distance=length(velocity);
    return velocity*min(1.0,effect.values0.z/max(distance,1e-5));
}
