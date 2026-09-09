/* Production GLSL attenuation, alias proposal and reservoir loop are extracted
 * by the runner. Only vector operations, storage and random inputs are mocked. */
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
using uint = uint32_t;
using std::min;
float min(float a, float b) { return std::min(a,b); }
float max(float a, float b) { return std::max(a,b); }
float inversesqrt(float a) { return 1/std::sqrt(a); }
float fract(float a) { return a-std::floor(a); }
float smoothstep(float a,float b,float x) {
    float t=std::clamp((x-a)/(b-a),0.0f,1.0f); return t*t*(3-2*t);
}
struct vec3 {
    float x,y,z;
    vec3(float a=0,float b=0,float c=0):x(a),y(b),z(c) {}
    vec3 operator-() const { return {-x,-y,-z}; }
    vec3 operator-(vec3 b) const { return {x-b.x,y-b.y,z-b.z}; }
    vec3 operator*(float b) const { return {x*b,y*b,z*b}; }
};
float dot(vec3 a,vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
vec3 normalize(vec3 a) { return a*inversesqrt(dot(a,a)); }
struct vec4 { float x=0,y=0,z=0,w=0; vec3 xyz() const { return {x,y,z}; } };
struct uvec4 { uint x=0,y=0,z=0,w=0; } lightCounts,gridDimensions;
static vec4 pointPositions[1056],pointColors[1056],pointCones[1056],lightAliases[1024];
static vec4 skyEnvironment;
static bool ptMapLightCull=false;
static bool ptAliasPDF=false;
static uint randomState,randomDraws,attenuationCalls;
float randomFloat() {
    ++randomDraws;
    randomState^=randomState<<13; randomState^=randomState>>17; randomState^=randomState<<5;
    return float(randomState>>8)*(1.0f/16777216.0f);
}
uint lightCell(vec3) { return 0; }
struct Result { uint selected,candidates; float total,weight; };
#define PT_CATEGORY(category)
#include "pt_map_light_math.inc"
static bool identical(float a,float b) { return std::memcmp(&a,&b,sizeof(a))==0; }
int main() {
    uint64_t cases=0,oldCalls=0,newCalls=0;
    for(uint count:{0u,1u,2u,8u,17u,128u,1024u}) {
        lightCounts.w=gridDimensions.w=count;
        for(uint distribution=0;distribution<2;++distribution) {
            float firstPDF=count ? 1.0f/count:0;
            for(uint i=0;i<count;++i) {
                float threshold=distribution && i ? 0.25f+0.7f*float(i%11)/10:1;
                lightAliases[i]={threshold,0,threshold/count,0};
                if(i) firstPDF+=(1-threshold)/count;
            }
            if(count) lightAliases[0].z=firstPDF;
            for(uint scene=0;scene<8;++scene) {
                for(uint i=0;i<count;++i) {
                    // Front/back, on plane, tiny distances, black lights, spot
                    // cone boundaries and shading/geometric normal disagreement.
                    float z=scene==0 ? -8.0f:scene==1 ? 8.0f:float(int(i%3)-1)*8;
                    float scale=scene==3 ? 1e-7f:1;
                    pointPositions[32+i]={float(int(i%7)-3)*scale,float(int(i%13)-6)*scale,z*scale,10.0f+i};
                    pointColors[32+i]=scene==4 || i%9==0 ? vec4{}:vec4{0.1f+float(i%5),0.2f,3.0f,0};
                    pointCones[32+i]=scene>=5 ? vec4{0,0,(i%2 ? -1.0f:1.0f),0.8f}:vec4{};
                }
                for(uint sample=1;sample<=10000;++sample) {
                    skyEnvironment.w=sample%2 ? 0:8;
                    vec3 start=scene==7 ? vec3{0.01f,-0.1f,0.02f}:vec3{};
                    vec3 geometric=scene==6 ? vec3{0,0,-1}:vec3{0,0,1};
                    vec3 n=scene>=6 ? normalize({0.7f,0.2f,0.1f}):geometric;
                    uint seed=sample*2654435761u;
                    randomState=seed; randomDraws=attenuationCalls=0; ptMapLightCull=false;
                    Result original=reservoir(start,n,geometric);
                    uint state=randomState,draws=randomDraws,calls=attenuationCalls;
                    oldCalls+=calls;
                    randomState=seed; randomDraws=attenuationCalls=0; ptMapLightCull=true;
                    Result candidate=reservoir(start,n,geometric);
                    assert(original.selected==candidate.selected && original.candidates==candidate.candidates);
                    assert(identical(original.total,candidate.total) && identical(original.weight,candidate.weight));
                    assert(randomState==state && randomDraws==draws);
                    assert(attenuationCalls<=calls);
                    newCalls+=attenuationCalls; ++cases;
                }
            }
        }
    }
    assert(newCalls<oldCalls);
    std::printf("%llu reservoir comparisons: exact selected light, weights and RNG; attenuation calls %llu -> %llu\n",
        (unsigned long long)cases,(unsigned long long)oldCalls,(unsigned long long)newCalls);
}
