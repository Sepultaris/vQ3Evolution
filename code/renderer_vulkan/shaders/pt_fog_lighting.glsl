// Isotropic volume scattering has no surface cosine or hemisphere rejection.
// Shadow queries include both solid occlusion and Beer-Lambert fog extinction.
vec3 fogLighting(vec3 world,bool lastBounce) {
    vec3 radiance=vec3(0);
    const float phase=1/(4*PI);
    for(uint slot=0u;slot<lightCounts.y+3u;++slot) {
        vec3 direction,incident;
        float distance,shadowDistance;
        if(slot==0u) {
            if(lightCounts.x==0u) continue;
            uint primitive=sampleEmitter();
            float root=sqrt(randomFloat());
            vec2 bary=vec2(root*(1-randomFloat()),0); bary.y=root-bary.x;
            vec3 cachedTarget,cachedNormal;
            float emitterPower;
            if(ptEmitterGeometry)
                sampledEmitterGeometry(primitive,sampledEmitterIndex,root,bary,cachedTarget,cachedNormal,emitterPower);
            uvec3 t=triangle(primitive);
            vec3 target=ptEmitterGeometry ? cachedTarget:
                position(t.x)*(1-root)+position(t.y)*bary.x+position(t.z)*bary.y;
            vec3 delta=target-world;
            distance=length(delta); direction=delta/max(distance,0.000001);
            float area;
            vec3 normal=ptEmitterGeometry ? cachedNormal:geometricNormal(primitive,area);
            float cosine=abs(dot(normal,-direction));
            if(cosine<0.0001 || distance<=0.04) continue;
            float pdf=ptEmitterGeometry ? emitterPower*(distance*distance)/max(lightSelection.x*cosine,0.000001):
                emitterPDF(primitive,distance*distance,cosine);
            // Explicit emitter coverage uses the light sampler's existing rule.
            incident=emissionAt(primitive,bary,false,world)*phase/max(pdf,0.00000001)*
                (lastBounce ? 1:powerWeight(pdf,phase));
        } else if(slot==1u) {
            if(!any(greaterThan(pc.sunRadiance.rgb,vec3(0)))) continue;
            direction=sampleSunPenumbra(normalize(pc.sunExposure.xyz)); distance=pc.parameters.y;
            incident=pc.sunRadiance.rgb*phase;
        } else {
            uint point; float weight=1;
            if(slot<lightCounts.y+2u) point=slot-2u;
            else {
                if(lightCounts.w==0u) continue;
                float pdf;
                point=32u+proposedLight(lightCell(world),randomFloat(),pdf);
                weight=1/max(pdf,0.00000001);
            }
            vec3 delta=pointPositions[point].xyz-world;
            distance=length(delta); direction=delta/max(distance,0.000001);
            if(distance<=0.04) continue;
            float lightScale=samplePointPenumbra(direction,distance,shadowDistance);
            float intensity=point<32u ? pointPositions[point].w*pointPositions[point].w/max(distance*distance,1):
                pointAttenuation(point,direction,distance*distance);
            incident=pointColors[point].rgb*(intensity*weight*phase*lightScale);
        }
        if(slot<2u) shadowDistance=distance;
        radiance+=incident*visibility(world,direction,slot==1u ? distance:shadowDistance-0.02);
    }
    return radiance;
}
#ifdef PT_PROFILE_PASS
vec3 timedFogLighting(vec3 world,bool lastBounce) {
    uint previous=profileEnter(9u);
    vec3 value=fogLighting(world,lastBounce);
    profileEnter(previous);
    return value;
}
#define fogLighting timedFogLighting
#endif
vec3 fogDirection() {
    float z=1-2*randomFloat(),angle=2*PI*randomFloat();
    float radius=sqrt(max(1-z*z,0));
    return vec3(radius*cos(angle),radius*sin(angle),z);
}
