#include "pt_rr_sampling_buffers.glsl"
#include "pt_reservoir_math.glsl"
uint rrRandomState;
float rrRandom() {
    rrRandomState=sampleHash(rrRandomState+0x9e3779b9u);
    return float(rrRandomState>>8)*(1.0/16777216.0);
}
float rrTarget(uint i,vec3 world,vec3 normal) {
    vec3 delta=pointPositions[i].xyz-world;
    float d2=max(dot(delta,delta),1);
    vec3 direction=delta*inversesqrt(d2);
    float power=max(dot(pointColors[i].rgb,vec3(0.2126,0.7152,0.0722)),0.000001);
    if(skyEnvironment.w>0) return power*(pointPositions[i].w/d2+0.000001);
    // A small positive floor preserves support when crossing a cone/horizon or
    // evaluating the same proposal at a different subpixel. Never reuse a
    // zero-support cosine-only reservoir at a new shading location.
    return power*(pointAttenuation(i,direction,d2)*max(dot(normal,direction),0)+0.000001);
}
bool rrValidReservoir(vec4 r) {
    return r.z>0 && r.y>0 && r.x>=32 && r.x<float(32u+lightCounts.w) &&
        !any(isnan(r)) && !any(isinf(r));
}
void rrMerge(inout vec4 r,inout float sum,vec4 other,vec3 world,vec3 normal,float cap) {
    if(!rrValidReservoir(other)) return;
    float count=min(other.z,cap);
    rrStream(r,sum,uint(other.x),rrTarget(uint(other.x),world,normal)*other.y*count,count,rrRandom());
}
bool rrSameSurface(RrSurface a,RrSurface b,float tolerance) {
    return floatBitsToUint(a.position.w)!=0u &&
        floatBitsToUint(a.position.w)==floatBitsToUint(b.position.w) &&
        dot(a.normal.xyz,b.normal.xyz)>0.95 && distance(a.position.xyz,b.position.xyz)<tolerance;
}
#ifdef PT_RR_GUIDES
bool rrAdaptiveSurface(RrSurface expected,RrSurface old,float footprint) {
    vec3 delta=old.position.xyz-expected.position.xyz;
    return rrAdaptiveSurfaceMatch(floatBitsToUint(expected.position.w),floatBitsToUint(old.position.w),
        dot(expected.normal.xyz,old.normal.xyz),length(delta),abs(dot(delta,expected.normal.xyz)),
        abs(expected.normal.w-old.normal.w),footprint);
}
void rrGuideSampling(uint index,vec3 world,vec3 normal,float roughness,uint identity,
    vec4 oldPosition,bool safe) {
    ivec2 size=imageSize(outputColor), pixel=ivec2(int(index)%size.x,int(index)/size.x);
    rrSurfaces[index]=RrSurface(vec4(world,uintBitsToFloat(identity)),vec4(normal,roughness));
    vec4 moments=vec4(0), previous=vec4(0);
    float footprint=max(0.05,length(world-pc.originNear.xyz)*pixelConeSpread());
    vec3 offset=oldPosition.xyz-rp.previousOrigin.xyz;
    float z=dot(offset,rp.previousForward.xyz);
    bool matched=false;
    bool adaptive=pc.sunRadiance.w>=2;
    bool budgetSafe=false;
    if(rp.jitterHistoryReset.w==0 && oldPosition.w>0 && z>0.001 && identity!=0u) {
        vec2 oldNdc=vec2(dot(offset,rp.previousRight.xyz)*rp.previousRight.w,
            dot(offset,rp.previousUp.xyz)*rp.previousUp.w)/z;
        vec2 uv=(oldNdc-rp.jitterHistoryReset.xy)*0.5+0.5;
        if(all(greaterThanEqual(uv,vec2(0))) && all(lessThan(uv,vec2(1)))) {
            ivec2 p=ivec2(uv*vec2(size));
            uint old=uint(p.y*size.x+p.x);
            RrSurface expected=rrSurfaces[index]; expected.position.xyz=oldPosition.xyz;
            matched=rrSameSurface(expected,rrOldSurfaces[old],2*footprint);
            if(matched) {
                if((uint(pc.sunRadiance.w)&1u)!=0u) previous=rrOldTemporal[old];
            }
            // Measure actual camera/object motion, excluding both Halton offsets.
            // History addresses above still include the old image's jitter.
            vec2 currentNdc=(vec2(pixel)+0.5)/vec2(size)*2-1+pc.parameters.zw;
            float movement=length((oldNdc-currentNdc)*0.5*vec2(size));
            if(adaptive && safe && matched && lightSelection.w==0) {
                if(movement<1) {
                    moments=rrOldAdaptive[old];
                    budgetSafe=true;
                } else if(movement<32*float(size.x)/1920) {
                    // A full bilinear footprint must track the same surface.
                    // Require every neighbor, even one with small weight: do not
                    // hide disocclusions by renormalizing only surviving taps.
                    vec2 address=uv*vec2(size)-0.5;
                    ivec2 base=ivec2(floor(address));
                    vec2 fraction=fract(address);
                    bool trusted=all(greaterThanEqual(base,ivec2(0))) &&
                        all(lessThan(base+1,size));
                    vec4 blended=vec4(0); float age=32;
                    for(int k=0;trusted && k<4;++k) {
                        ivec2 corner=ivec2(k&1,k>>1), tap=base+corner;
                        uint tapIndex=uint(tap.y*size.x+tap.x);
                        vec4 history=rrOldAdaptive[tapIndex];
                        trusted=rrAdaptiveSurface(expected,rrOldSurfaces[tapIndex],footprint) &&
                            rrMovingHistorySafe(movement,32*float(size.x)/1920,history) &&
                            !any(isnan(history)) && !any(isinf(history));
                        vec2 weight=mix(1-fraction,fraction,vec2(corner));
                        blended.xy+=history.xy*weight.x*weight.y;
                        age=min(age,history.z);
                    }
                    if(trusted) {
                        moments=vec4(blended.xy,age,0);
                        budgetSafe=true;
                    } else {
                        // Keep accumulating new observations after an uncertain
                        // match; otherwise the 12-frame gate could never mature
                        // during motion. Never carry old color or use it as RR input.
                        vec4 history=rrOldAdaptive[old];
                        if(rrAdaptiveSurface(expected,rrOldSurfaces[old],footprint) &&
                            !any(isnan(history)) && !any(isinf(history))) moments=history;
                    }
                }
            }
        }
    }
    if(adaptive) rrAdaptive[index]=vec4(moments.xyz,
        rrSampleBudget(pc.sampling.x,moments,budgetSafe));
    vec4 r=vec4(0); float sum=0;
    bool reuse=(uint(pc.sunRadiance.w)&1u)!=0u;
    if(reuse && identity!=0u && lightCounts.w>0u) {
        rrRandomState=sampleHash(index+uint(pc.sampling.z)*747796405u+91u);
        // The existing cell distribution has a 20% uniform support floor.
        // Keep its exact PDF, including when reusing across different cells.
        uint cell=lightCell(world);
        for(int k=0;k<2;++k) {
            float pdf;
            uint i=32u+proposedLight(cell,rrRandom(),pdf);
            rrStream(r,sum,i,rrTarget(i,world,normal)/pdf,1,rrRandom());
        }
        bool keepHistory=matched && rrValidReservoir(previous) && previous.w<8;
        if(keepHistory) rrMerge(r,sum,previous,world,normal,8);
        r.y=rrNormalize(sum,r.z,rrTarget(uint(r.x),world,normal));
        // Deterministic periodic expiry prevents indefinite correlated history.
        r.w=keepHistory ? previous.w+1:0;
    }
    rrTemporal[index]=r;
}
#endif
#ifdef PT_RR_TRACE
uint rrPathSample;
bool rrPrimaryLight(int bounce,vec3 world,vec3 normal,out uint selected,out float weight) {
    if((uint(pc.sunRadiance.w)&1u)==0u || bounce!=0 || rrPathSample!=0u) return false;
    vec4 r=rrSpatial[samplePixel];
    if(!rrValidReservoir(r)) return false;
    RrSurface surface=rrSurfaces[samplePixel];
    float footprint=max(0.05,length(world-pc.originNear.xyz)*pixelConeSpread());
    if(distance(surface.position.xyz,world)>2*footprint || dot(surface.normal.xyz,normal)<0.95) return false;
    selected=uint(r.x); weight=r.y;
    return true;
}
#endif
