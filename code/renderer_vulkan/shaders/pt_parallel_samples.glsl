// Four independent samples of a 4x4 pixel tile. Only 4 KiB of workgroup-local
// scratch, no resolution-sized allocation, global atomics or extra dispatch.
#if defined(PT_PROFILE_PASS) || defined(PT_GUIDE_PASS)
#error Parallel samples are a lighting-only pass; guides/clock diagnostics keep their own layout.
#endif
#include "pt_sample_seed.glsl"
shared vec4 ptSampleRadiance[256];
void main() {
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy), size=imageSize(outputColor);
    bool valid=all(lessThan(pixel,size));
    uint lane=gl_LocalInvocationID.z;
    uint localPixel=gl_LocalInvocationID.y*4u+gl_LocalInvocationID.x;
    uint slot=(lane*16u+localPixel)*4u;
    uint index=uint(pixel.y*size.x+pixel.x);
    // Edge lanes must initialize scratch and participate in the barrier too.
    // Compile-only experiment: this kernel assumes exactly four samples.
    vec3 d=vec3(0),s=vec3(0),t=vec3(0),e=vec3(0);
    if(valid) {
        samplePixel=index; sampleNumber=uint(pc.sampling.z)*4u+lane; sampleDimension=0;
        sampleCoordinate=uvec2(pixel); sampleTile=sampleHash((uint(pixel.x)>>6)+4099u*(uint(pixel.y)>>6));
        rng=independentPathSeed(index,uint(pc.sampling.z),lane);
        vec2 jitter=vec2(randomFloat(),randomFloat())-0.5;
        integrator(pc.originNear.xyz,primaryDirection(vec2(pixel)+0.5+jitter,size));
        if(!any(isnan(radianceD)) && !any(isinf(radianceD))) d=radianceD;
        if(!any(isnan(radianceS)) && !any(isinf(radianceS))) s=radianceS;
        if(!any(isnan(radianceT)) && !any(isinf(radianceT))) t=radianceT;
        if(!any(isnan(radianceE)) && !any(isinf(radianceE))) e=radianceE;
    }
    ptSampleRadiance[slot]=vec4(d,0);
    ptSampleRadiance[slot+1u]=vec4(s,0);
    ptSampleRadiance[slot+2u]=vec4(t,0);
    ptSampleRadiance[slot+3u]=vec4(e,0);
    barrier();
    if(!valid || lane!=0u) return;
    vec3 diffuse=vec3(0),reflection=vec3(0),transmitted=vec3(0),emission=vec3(0);
    // The same sample-order sum as serial integration, not unordered atomics.
    for(uint sampleIndex=0;sampleIndex<4u;++sampleIndex) {
        uint source=(sampleIndex*16u+localPixel)*4u;
        diffuse+=ptSampleRadiance[source].rgb;
        reflection+=ptSampleRadiance[source+1u].rgb;
        transmitted+=ptSampleRadiance[source+2u].rgb;
        emission+=ptSampleRadiance[source+3u].rgb;
    }
    diffuse/=pc.sampling.x; reflection/=pc.sampling.x; transmitted/=pc.sampling.x; emission/=pc.sampling.x;
    float history=pc.sampling.w;
    if(history>0) {
        diffuse=(accumulated[index].rgb*history+diffuse)/(history+1);
        reflection=(specular[index].rgb*history+reflection)/(history+1);
        transmitted=(transmission[index].rgb*history+transmitted)/(history+1);
        emission=(visibleEmission[index].rgb*history+emission)/(history+1);
    }
    accumulated[index]=vec4(diffuse,history+1);
    specular[index]=vec4(reflection,history+1);
    transmission[index]=vec4(transmitted,history+1);
    visibleEmission[index]=vec4(emission,history+1);
    vec3 result=rp.options.z==1 ? diffuse : (rp.options.z==2 ? reflection+transmitted : (rp.options.z==3 ? emission : diffuse+reflection+transmitted+emission));
    if(rp.options.z<4) imageStore(outputColor,pixel,vec4(toneMap(result),1));
}
