// Included inside integrator after prepareHitBRDF. Preserve the original light
// order and RNG consumption, but share ONE inlined visibility/BRDF call site.
// No reservoir across light categories, fewer rays, or omitted light sources
// by default. The opt-in dynamic-light reservoir (ptDlightReservoir) replaces
// per-source dlight shadow rays at light slot 2 with a single sampled source.
for(uint lightSlot=0;lightSlot<lightCounts.y+3u;++lightSlot) {
    vec3 l;
    float distance, nl=0, lightPDF=0;
    uint primitive=0, point=0;
    vec2 bary=vec2(0);
    float totalWeight=0, selectedWeight=0;
    uint candidateCount=0;
    if(lightSlot==0u) {
        PT_CATEGORY(3u);
        if(lightCounts.x==0u) continue;
        primitive=sampleEmitter();
        float root=sqrt(randomFloat());
        bary=vec2(root*(1-randomFloat()),0);
        bary.y=root-bary.x;
        vec3 cachedTarget,cachedNormal;
        float emitterPower;
        if(ptEmitterGeometry)
            sampledEmitterGeometry(primitive,sampledEmitterIndex,root,bary,cachedTarget,cachedNormal,emitterPower);
        uvec3 t=triangle(primitive);
        vec3 target=ptEmitterGeometry ? cachedTarget:
            position(t.x)*(1-root)+position(t.y)*bary.x+position(t.z)*bary.y;
        vec3 delta=target-start;
        distance=length(delta);
        float area;
        l=delta/max(distance,0.000001);
        vec3 ln=ptEmitterGeometry ? cachedNormal:geometricNormal(primitive,area);
        nl=max(dot(n,l),0);
        float lc=abs(dot(ln,-l));
        lightPDF=ptEmitterGeometry ? emitterPower*(distance*distance)/max(lightSelection.x*lc,0.000001):
            emitterPDF(primitive,distance*distance,lc);
        if(!(nl>0 && dot(geometric,l)>0 && lc>0.0001 && distance>0.04)) continue;
    } else if(lightSlot==1u) {
        PT_CATEGORY(7u);
        l=normalize(pc.sunExposure.xyz);
        if(any(greaterThan(pc.sunRadiance.rgb,vec3(0)))) l=sampleSunPenumbra(l);
        distance=pc.parameters.y;
        if(!(dot(n,l)>0 && dot(geometric,l)>0 && any(greaterThan(pc.sunRadiance.rgb,vec3(0))))) continue;
    } else if(lightSlot<lightCounts.y+2u) {
        PT_CATEGORY(7u);
        if(ptDlightReservoir) {
            if(lightSlot!=2u) continue;
            candidateCount=lightCounts.y;
            uint selected=0;
            for(uint candidate=0;candidate<candidateCount;++candidate) {
                vec3 delta=pointPositions[candidate].xyz-start;
                float distanceSquared=dot(delta,delta);
                vec3 direction=delta*inversesqrt(max(distanceSquared,0.000001));
                float weight=dot(pointColors[candidate].rgb,vec3(0.2126,0.7152,0.0722))*pointAttenuation(candidate,direction,distanceSquared)*max(dot(n,direction),0);
                if(skyEnvironment.w>0) weight=dot(pointColors[candidate].rgb,vec3(0.2126,0.7152,0.0722))*
                    pointPositions[candidate].w/max(distanceSquared,1);
                if(weight<=0) continue;
                float proposalPDF=1.0/float(lightCounts.y);
                float reservoirWeight=weight/max(proposalPDF,0.00000001);
                totalWeight+=reservoirWeight;
                if(randomFloat()*totalWeight<reservoirWeight) { selected=candidate; selectedWeight=weight; }
            }
            if(selectedWeight<=0) continue;
            point=selected;
            vec3 delta=pointPositions[selected].xyz-start;
            distance=length(delta);
            l=delta/max(distance,0.000001);
            if(distance<=0.04 || (skyEnvironment.w<=0 && (dot(n,l)<=0 || dot(geometric,l)<=0))) continue;
        } else {
            point=lightSlot-2u;
            vec3 delta=pointPositions[point].xyz-start;
            distance=length(delta);
            l=delta/max(distance,0.000001);
            if(distance<=0.04 || (skyEnvironment.w<=0 && (dot(n,l)<=0 || dot(geometric,l)<=0))) continue;
        }
    } else {
        PT_CATEGORY(4u);
        uint selected=0;
#ifdef PT_RR_TRACE
        float reusedWeight;
        if(rrPrimaryLight(bounce,world,n,selected,reusedWeight)) {
            totalWeight=reusedWeight; selectedWeight=1; candidateCount=1;
        } else {
#endif
        uint cell=lightCell(start);
        candidateCount=min(lightCounts.w,8u);
        for(uint candidate=0;candidate<candidateCount;++candidate) {
            float proposalPDF;
            uint i=32+proposedLight(cell,randomFloat(),proposalPDF);
            vec3 delta=pointPositions[i].xyz-start;
            float distanceSquared=dot(delta,delta);
            vec3 direction=delta*inversesqrt(max(distanceSquared,0.000001));
            if(skyEnvironment.w<=0 && ptMapLightCull && dot(geometric,direction)<=0) continue;
            float weight=dot(pointColors[i].rgb,vec3(0.2126,0.7152,0.0722))*pointAttenuation(i,direction,distanceSquared)*max(dot(n,direction),0);
            if(skyEnvironment.w>0) weight=dot(pointColors[i].rgb,vec3(0.2126,0.7152,0.0722))*
                pointPositions[i].w/max(distanceSquared,1);
            if((skyEnvironment.w<=0 && !ptMapLightCull && dot(geometric,direction)<=0) || weight<=0) continue;
            float reservoirWeight=weight/max(proposalPDF,0.00000001);
            totalWeight+=reservoirWeight;
            if(randomFloat()*totalWeight<reservoirWeight) { selected=i; selectedWeight=weight; }
        }
#ifdef PT_RR_TRACE
        }
#endif
        PT_CATEGORY(7u);
        if(selectedWeight<=0) continue;
        point=selected;
        vec3 delta=pointPositions[selected].xyz-start;
        distance=length(delta);
        l=delta/max(distance,0.000001);
        if(distance<=0.04 || (skyEnvironment.w<=0 && (dot(n,l)<=0 || dot(geometric,l)<=0))) continue;
    }
    float shadowDistance=distance,lightScale=1;
    if(lightSlot>=2u) {
        lightScale=samplePointPenumbra(l,distance,shadowDistance);
        if(dot(n,l)<=0 || dot(geometric,l)<=0) continue;
    }
    vec3 transmittance=visibility(start,l,lightSlot==1u ? distance:shadowDistance-0.02);
    if(!any(greaterThan(transmittance,vec3(0)))) continue;
    PT_CATEGORY(5u);
    float pdf;
    vec3 brdf=evaluateHitBRDF(n,v,l,albedo,roughness,metallic,pdf);
    if(lightSlot==0u) {
        float weight=bounce+1<int(pc.sampling.y) ? powerWeight(lightPDF,pdf) : 1;
        addDirect(brdf,emissionAt(primitive,bary,false,start)*nl/lightPDF*weight*transmittance);
    } else if(lightSlot==1u) {
        addDirect(brdf,max(dot(n,l),0)*pc.sunRadiance.rgb*transmittance);
    } else if(lightSlot<lightCounts.y+2u) {
        if(ptDlightReservoir && candidateCount>0u) {
            vec3 incident=pointColors[point].rgb*pointAttenuation(point,l,distance*distance);
            addDirect(brdf,dot(n,l)*incident*totalWeight/selectedWeight/float(candidateCount)*lightScale*transmittance);
        } else {
            float intensity=pointPositions[point].w*pointPositions[point].w/max(distance*distance,1);
            addDirect(brdf,dot(n,l)*pointColors[point].rgb*intensity*lightScale*transmittance);
        }
    } else {
        vec3 incident=pointColors[point].rgb*pointAttenuation(point,l,distance*distance);
        addDirect(brdf,dot(n,l)*incident*totalWeight/selectedWeight/float(candidateCount)*lightScale*transmittance);
    }
}
