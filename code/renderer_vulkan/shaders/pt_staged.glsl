// Experimental two-stage transport. The original integrator remains the
// default/reference until this path passes image and measured GPU comparisons.
// One pixel owns one state; samples remain SERIAL with the original RNG stream.
struct StagedState {
    uvec4 control; // mode, RNG, dimension, sample ordinal
    uvec4 integers; // medium count, transparent layers, path class, bounce
    vec4 originCone, directionSpread, misc;
    vec4 media[4];
    vec4 weightA, weightB;
    vec4 radiance[4], sum[4];
    vec4 normalRough, geometricMetal, albedoValue, startPoint, footprint;
}; // std430: 384 bytes; native allocation and offline checker assert the stride.
layout(set=1,binding=0,std430) buffer StagedPaths { StagedState paths[]; };
layout(set=1,binding=1,std430) buffer StagedComparison {
    uint compared, mismatched, nonfinite, maxRelative, maxAbsolute;
} comparison;
uint pathIndex;
vec3 origin,direction,n,geometric,albedo,start;
float coneWidth,coneSpread,previousPDF,segmentDistance,roughness,metallic;
float mediumIOR[4];
vec3 mediumAbsorption[4];
int mediumCount,transparentLayers,bounce;

void stagedLoadTransport() {
    rng=paths[pathIndex].control.y;
    sampleDimension=paths[pathIndex].control.z;
    sampleNumber=uint(pc.sampling.z)*uint(pc.sampling.x)+paths[pathIndex].control.w;
    pathClass=paths[pathIndex].integers.z;
    pathA=paths[pathIndex].weightA.xyz; pathB=paths[pathIndex].weightB.xyz;
    radianceD=paths[pathIndex].radiance[0].xyz; radianceS=paths[pathIndex].radiance[1].xyz;
    radianceT=paths[pathIndex].radiance[2].xyz; radianceE=paths[pathIndex].radiance[3].xyz;
}
void stagedSaveTransport() {
    paths[pathIndex].control.yz=uvec2(rng,sampleDimension);
    paths[pathIndex].integers.z=pathClass;
    paths[pathIndex].weightA=vec4(pathA,0); paths[pathIndex].weightB=vec4(pathB,0);
    paths[pathIndex].radiance[0]=vec4(radianceD,0); paths[pathIndex].radiance[1]=vec4(radianceS,0);
    paths[pathIndex].radiance[2]=vec4(radianceT,0); paths[pathIndex].radiance[3]=vec4(radianceE,0);
}
void stagedLoadRay() {
    origin=paths[pathIndex].originCone.xyz; coneWidth=paths[pathIndex].originCone.w;
    direction=paths[pathIndex].directionSpread.xyz; coneSpread=paths[pathIndex].directionSpread.w;
    previousPDF=paths[pathIndex].misc.x; segmentDistance=paths[pathIndex].misc.y;
    bounce=int(paths[pathIndex].integers.w);
}
void stagedSaveRay() {
    paths[pathIndex].originCone=vec4(origin,coneWidth);
    paths[pathIndex].directionSpread=vec4(direction,coneSpread);
    paths[pathIndex].misc.xy=vec2(previousPDF,segmentDistance);
}
void stagedBeginSample(ivec2 pixel,ivec2 size,uint ordinal) {
    // Do NOT reseed rng here: later sample streams depend on earlier paths.
    sampleNumber=uint(pc.sampling.z)*uint(pc.sampling.x)+ordinal;
    sampleDimension=0;
    vec2 jitter=vec2(randomFloat(),randomFloat())-0.5;
    paths[pathIndex].control=uvec4(1u,rng,sampleDimension,ordinal);
    paths[pathIndex].integers=uvec4(rp.cameraMedium.x>1 ? 1u:0u,0u,0u,0u);
    paths[pathIndex].originCone=vec4(pc.originNear.xyz,0);
    paths[pathIndex].directionSpread=vec4(primaryDirection(vec2(pixel)+0.5+jitter,size),pixelConeSpread());
    paths[pathIndex].misc=vec4(0,0,uintBitsToFloat(0xffffffffu),0);
    for(int i=0;i<4;++i) paths[pathIndex].media[i]=i==0 ? rp.cameraMedium:vec4(0);
    resetCompactPath();
    stagedSaveTransport();
}
#if PT_STAGED_PASS == 1
#include "pt_staged_surface.glsl"
#elif PT_STAGED_PASS == 2
#include "pt_staged_lighting.glsl"
#endif

void stagedCompare(vec3 a,vec3 b,inout bool bad,inout bool invalid,inout float relative,inout float absolute) {
    invalid=invalid || any(isnan(a)) || any(isinf(a)) || any(isnan(b)) || any(isinf(b));
    vec3 difference=abs(a-b);
    vec3 scaled=difference/max(max(abs(a),abs(b)),vec3(0.001));
    bad=bad || any(greaterThan(difference,vec3(0.00001)+0.0001*max(abs(a),abs(b))));
    relative=max(relative,max(scaled.x,max(scaled.y,scaled.z)));
    absolute=max(absolute,max(difference.x,max(difference.y,difference.z)));
}
void main() {
    int firstRow=int(pc.forwardFar.w<0 ? -pc.forwardFar.w-1:pc.forwardFar.w);
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy)+ivec2(0,firstRow),size=imageSize(outputColor);
    if(any(greaterThanEqual(pixel,size))) return;
    pathIndex=gl_GlobalInvocationID.y*uint(size.x)+gl_GlobalInvocationID.x;
    samplePixel=uint(pixel.y*size.x+pixel.x); sampleCoordinate=uvec2(pixel);
    sampleTile=sampleHash((uint(pixel.x)>>6)+4099u*(uint(pixel.y)>>6));
#if PT_STAGED_PASS == 0
    rng=(samplePixel+1u)*747796405u+(uint(pc.sampling.z)+1u)*2891336453u;
    if(rng==0u) rng=1u;
    for(int i=0;i<4;++i) paths[pathIndex].sum[i]=vec4(0);
    stagedBeginSample(pixel,size,0u);
#elif PT_STAGED_PASS == 1
    if(paths[pathIndex].control.x==0u) return;
    stagedLoadTransport(); stagedLoadRay();
    mediumCount=int(paths[pathIndex].integers.x);
    transparentLayers=int(paths[pathIndex].integers.y);
    for(int i=0;i<mediumCount;++i) {
        mediumIOR[i]=paths[pathIndex].media[i].x;
        mediumAbsorption[i]=paths[pathIndex].media[i].yzw;
    }
    uint mode=stagedSurface();
    paths[pathIndex].control.x=mode;
    stagedSaveTransport(); stagedSaveRay();
    paths[pathIndex].integers.xy=uvec2(mediumCount,transparentLayers);
    for(int i=0;i<mediumCount;++i) paths[pathIndex].media[i]=vec4(mediumIOR[i],mediumAbsorption[i]);
    if(mode==2u) {
        paths[pathIndex].normalRough=vec4(n,roughness);
        paths[pathIndex].geometricMetal=vec4(geometric,metallic);
        paths[pathIndex].albedoValue=vec4(albedo,0);
        paths[pathIndex].startPoint=vec4(start,0);
        paths[pathIndex].misc.z=uintBitsToFloat(footprintPrimitive);
        paths[pathIndex].footprint=vec4(footprintBarycentrics[0],footprintBarycentrics[1]);
    }
#elif PT_STAGED_PASS == 2
    uint mode=paths[pathIndex].control.x;
    if(mode==0u) return;
    if(mode==2u) {
        stagedLoadTransport(); stagedLoadRay();
        n=paths[pathIndex].normalRough.xyz; roughness=paths[pathIndex].normalRough.w;
        geometric=paths[pathIndex].geometricMetal.xyz; metallic=paths[pathIndex].geometricMetal.w;
        albedo=paths[pathIndex].albedoValue.xyz; start=paths[pathIndex].startPoint.xyz;
        uint mediaCount=paths[pathIndex].integers.x;
        visibilityAbsorption=mediaCount>0u ? paths[pathIndex].media[mediaCount-1u].yzw:vec3(0);
        footprintPrimitive=floatBitsToUint(paths[pathIndex].misc.z);
        footprintBarycentrics=mat2(paths[pathIndex].footprint.xy,paths[pathIndex].footprint.zw);
        paths[pathIndex].control.x=stagedLighting();
        stagedSaveTransport(); stagedSaveRay();
    }
    paths[pathIndex].integers.w++;
#else
    vec3 diffuse=paths[pathIndex].sum[0].xyz, reflection=paths[pathIndex].sum[1].xyz;
    vec3 transmitted=paths[pathIndex].sum[2].xyz, emission=paths[pathIndex].sum[3].xyz;
    stagedLoadTransport();
    if(!any(isnan(radianceD)) && !any(isinf(radianceD))) diffuse+=radianceD;
    if(!any(isnan(radianceS)) && !any(isinf(radianceS))) reflection+=radianceS;
    if(!any(isnan(radianceT)) && !any(isinf(radianceT))) transmitted+=radianceT;
    if(!any(isnan(radianceE)) && !any(isinf(radianceE))) emission+=radianceE;
    uint ordinal=paths[pathIndex].control.w+1u;
    if(ordinal<uint(pc.sampling.x)) {
        paths[pathIndex].sum[0]=vec4(diffuse,0); paths[pathIndex].sum[1]=vec4(reflection,0);
        paths[pathIndex].sum[2]=vec4(transmitted,0); paths[pathIndex].sum[3]=vec4(emission,0);
        stagedBeginSample(pixel,size,ordinal);
        return;
    }
    diffuse/=pc.sampling.x; reflection/=pc.sampling.x; transmitted/=pc.sampling.x; emission/=pc.sampling.x;
    // Diagnostic paired rendering uses the SAME frame, ray sequence and scene,
    // before denoising. Reference history must be zero for this optional check.
    if(pc.forwardFar.w<0 && pc.sampling.w==0) {
        bool bad=false,invalid=false; float relative=0,absolute=0;
        stagedCompare(diffuse,accumulated[samplePixel].rgb,bad,invalid,relative,absolute);
        stagedCompare(reflection,specular[samplePixel].rgb,bad,invalid,relative,absolute);
        stagedCompare(transmitted,transmission[samplePixel].rgb,bad,invalid,relative,absolute);
        stagedCompare(emission,visibleEmission[samplePixel].rgb,bad,invalid,relative,absolute);
        atomicAdd(comparison.compared,1u);
        if(bad) atomicAdd(comparison.mismatched,1u);
        if(invalid) atomicAdd(comparison.nonfinite,1u);
        atomicMax(comparison.maxRelative,floatBitsToUint(relative));
        atomicMax(comparison.maxAbsolute,floatBitsToUint(absolute));
    }
    float history=pc.sampling.w;
    if(history>0) {
        diffuse=(accumulated[samplePixel].rgb*history+diffuse)/(history+1);
        reflection=(specular[samplePixel].rgb*history+reflection)/(history+1);
        transmitted=(transmission[samplePixel].rgb*history+transmitted)/(history+1);
        emission=(visibleEmission[samplePixel].rgb*history+emission)/(history+1);
    }
    accumulated[samplePixel]=vec4(diffuse,history+1); specular[samplePixel]=vec4(reflection,history+1);
    transmission[samplePixel]=vec4(transmitted,history+1); visibleEmission[samplePixel]=vec4(emission,history+1);
    vec3 result=rp.options.z==1 ? diffuse : (rp.options.z==2 ? reflection+transmitted : (rp.options.z==3 ? emission : diffuse+reflection+transmitted+emission));
    if(rp.options.z<4) imageStore(outputColor,pixel,vec4(toneMap(result),1));
#endif
}
