/* Execute the native CDF-index builder and production GLSL sampler on CPU.
 * Only GLSL vectors, the random input and storage-buffer reads are mocked. */
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
using uint = uint32_t;
using std::min;
struct uvec4 {
    uint x=0,y=0,z=0,w=0;
    uint operator[](uint i) const { return i==0 ? x : (i==1 ? y : (i==2 ? z:w)); }
};
struct vec4 { float x=0,y=0,z=0,w=0; } lightSelection;
typedef struct { uint32_t primitive; float cumulative_power; } pt_emitter_t;
#include "pt_emitter_search.h"
struct Emitter { uint primitive; float cumulativePower; };
static uint64_t cdfReads=0;
struct EmitterTable {
    Emitter data[8192];
    const Emitter &operator[](uint i) const { assert(i<8192); ++cdfReads; return data[i]; }
} emitters;
static uvec4 lightCounts,emitterSearchControl,emitterSearchRanges[32];
static float randomInput;
static uint randomDraws=0;
float randomFloat() { ++randomDraws; return randomInput; }
#include "pt_emitter_sampler.inc"

static uint64_t comparisons=0,originalReads=0,indexedReads=0;
static uint randomState=0xabc9513u;
static uint nextRandom() {
    randomState^=randomState<<13; randomState^=randomState>>17; randomState^=randomState<<5;
    return randomState;
}
static void compare(float u) {
    if(!(u>=0 && u<=1)) return;
    randomInput=u;
    emitterSearchControl.x=0;
    randomDraws=0; cdfReads=0;
    uint reference=sampleEmitter();
    assert(randomDraws==1);
    originalReads+=cdfReads;
    emitterSearchControl.x=1;
    randomDraws=0; cdfReads=0;
    uint indexed=sampleEmitter();
    if(indexed!=reference) {
        std::printf("Mismatch: count=%u u=%.9g total=%.9g original=%u indexed=%u\n",
            lightCounts.x,u,lightSelection.x,reference,indexed);
        std::abort();
    }
    assert(randomDraws==1);
    indexedReads+=cdfReads;
    ++comparisons;
}
static void check(const std::vector<float> &weights) {
    std::vector<pt_emitter_t> native(weights.size());
    float sum=0;
    for(uint i=0;i<weights.size();++i) {
        sum+=weights[i];
        native[i]={i*3u+9u,sum};
        emitters.data[i]={native[i].primitive,sum};
    }
    assert(std::isfinite(sum));
    lightCounts.x=uint(weights.size()); lightSelection.x=sum;
    uint ranges[64][2];
    build_emitter_search(native.data(),uint(native.size()),sum,ranges);
    for(uint bucket=0;bucket<64;++bucket) {
        assert(ranges[bucket][0]<=ranges[bucket][1]);
        assert(ranges[bucket][1]<weights.size());
    }
    for(uint i=0;i<32;++i)
        emitterSearchRanges[i]={ranges[i*2][0],ranges[i*2][1],ranges[i*2+1][0],ranges[i*2+1][1]};
    // Exact ties, adjacent representable inputs, empty/zero-mass buckets,
    // partial last buckets and a float-rounded target at the next boundary.
    for(uint bucket=0;bucket<=64;++bucket) {
        float u=float(bucket)/64;
        compare(u); compare(std::nextafter(u,0.0f)); compare(std::nextafter(u,1.0f));
    }
    for(const auto &emitter:native) {
        float u=sum>0 ? emitter.cumulative_power/sum:0;
        compare(u); compare(std::nextafter(u,0.0f)); compare(std::nextafter(u,1.0f));
    }
    for(uint i=0;i<30000;++i) compare(float(nextRandom()>>8)*(1.0f/16777216.0f));
}
int main() {
    uint empty[64][2];
    std::memset(empty,0xff,sizeof(empty));
    build_emitter_search(nullptr,0,0,empty);
    for(auto &range:empty) assert(range[0]==0 && range[1]==0);
    for(uint count:{1u,2u,63u,64u,65u,1700u,8192u}) {
        check(std::vector<float>(count,1));
        check(std::vector<float>(count,0));
        for(uint hot:{0u,count/2,count-1}) {
            std::vector<float> weights(count,1e-8f);
            weights[hot]=1e8f;
            check(weights);
        }
        for(int round=0;round<4;++round) {
            std::vector<float> weights(count);
            for(float &weight:weights)
                weight=std::ldexp(float((nextRandom()&1023)+1),int(nextRandom()%40)-20);
            check(weights); // Rebuild after area/power/ordering changes.
            std::reverse(weights.begin(),weights.end());
            check(weights);
        }
    }
    // Powers near underflow exercise outward-rounded native boundaries too.
    check({0,1e-40f,1e-39f,0,1e-38f,1e-37f});
    std::printf("PASS: %llu production-sampler comparisons; identical primitive and one random draw, including ties/rebuilds\n",
        (unsigned long long)comparisons);
    std::printf("CDF record reads across adversarial fixtures: original %llu, indexed %llu (not GPU timing)\n",
        (unsigned long long)originalReads,(unsigned long long)indexedReads);
}
