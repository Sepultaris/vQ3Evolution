// Execute both production GLSL lighting bodies with deterministic scene hooks.
// Checks every shadow ray, direct-light contribution, and RNG state/order.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
using uint=uint32_t;
struct vec2 { float x,y; vec2(float x=0,float y=0):x(x),y(y){} };
struct vec3 { float x,y,z; vec3(float x=0):x(x),y(x),z(x){} vec3(float x,float y,float z):x(x),y(y),z(z){} };
struct vec4 { vec3 xyz; float w; vec4(vec3 xyz=vec3(0),float w=0):xyz(xyz),w(w){} };
struct uvec3 { uint x,y,z; };
struct uvec4 { uint x,y,z,w; };
static vec3 operator+(vec3 a,vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
static vec3 operator-(vec3 a,vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
static vec3 operator-(vec3 a){return {-a.x,-a.y,-a.z};}
static vec3 operator*(vec3 a,vec3 b){return {a.x*b.x,a.y*b.y,a.z*b.z};}
static vec3 operator/(vec3 a,vec3 b){return {a.x/b.x,a.y/b.y,a.z/b.z};}
static float dot(vec3 a,vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static vec3 cross(vec3 a,vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static float length(vec3 a){return std::sqrt(dot(a,a));}
static float sqrt(float a){return std::sqrt(a);}
static float inversesqrt(float a){return 1/std::sqrt(a);}
static vec3 normalize(vec3 a){return a/length(a);}
static float max(float a,float b){return std::max(a,b);}
static uint min(uint a,uint b){return std::min(a,b);}
static bool greaterThan(vec3 a,vec3 b){return a.x>b.x || a.y>b.y || a.z>b.z;}
static bool any(bool b){return b;}
using std::abs;
static struct { vec4 sunExposure, sunRadiance, parameters; struct { float x,y,z,w; } sampling; } pc;
static uvec4 lightCounts;
static struct { float x; } lightSelection;
static vec4 pointPositions[1056],pointColors[1056];
static vec3 positions[192];
static bool ptEmitterGeometry,ptMapLightCull,ptDlightReservoir;
static uint sampledEmitterIndex, rng, trial;
static std::vector<vec4> rays;
static std::vector<vec3> contributions;
static vec3 diffuseBRDF;
static vec4 skyEnvironment;
static const float PI=3.14159265358979323846f;
static float sin(float x){return std::sin(x);}
static float cos(float x){return std::cos(x);}
static vec3 basisSample(vec3 n,vec3 local){
    vec3 tangent=normalize(cross(abs(n.z)<.999f ? vec3(0,0,1):vec3(0,1,0),n));
    return tangent*local.x+cross(n,tangent)*local.y+n*local.z;
}
static float randomFloat(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return float(rng>>8)*(1.0f/16777216.0f);}
#include "pt_penumbra.inc"
static uint sampleEmitter(){sampledEmitterIndex=uint(randomFloat()*float(lightCounts.x));return sampledEmitterIndex;}
static uvec3 triangle(uint i){return {i*3,i*3+1,i*3+2};}
static vec3 position(uint i){return positions[i];}
static vec3 geometricNormal(uint i,float &area){uvec3 t=triangle(i);vec3 n=cross(position(t.y)-position(t.x),position(t.z)-position(t.x));area=length(n);return n/max(area,0.000001f);}
static void sampledEmitterGeometry(uint primitive,uint index,float root,vec2 bary,vec3 &target,vec3 &normal,float &power){
    assert(primitive==index);uvec3 t=triangle(primitive);float area;
    target=position(t.x)*(1-root)+position(t.y)*bary.x+position(t.z)*bary.y;
    normal=geometricNormal(primitive,area);power=float(primitive+1);
}
static float emitterPDF(uint primitive,float d2,float cosine){return float(primitive+1)*d2/max(lightSelection.x*cosine,0.000001f);}
static float pointAttenuation(uint i,vec3 l,float d2){return pointPositions[i].w*max(l.z+1,0)/max(d2,1);}
static uint lightCell(vec3 start){return uint(abs(start.x))*17u%512u;}
static uint proposedLight(uint cell,float r,float &pdf){uint count=lightCounts.z;assert(count);pdf=1.0f/float(count);return (uint(r*float(count))+cell)%count;}
static float powerWeight(float a,float b){return a*a/max(a*a+b*b,0.000000001f);}
static vec3 visibility(vec3 start,vec3 l,float distance){
    rays.push_back({start,0});rays.push_back({l,distance});
    uint kind=(uint(rays.size()/2)+trial)%7;
    return kind==0 ? vec3(0):kind==1 ? vec3(.2f,.7f,.9f):vec3(1);
}
static vec3 evaluateHitBRDF(vec3 n,vec3 v,vec3 l,vec3 albedo,float roughness,float metallic,float &pdf){
    pdf=max(dot(n,l),0)*.25f+max(dot(v,l),0)*.75f;
    diffuseBRDF=albedo*(1-metallic)*max(dot(n,l),0);
    return diffuseBRDF+vec3(roughness*pdf);
}
static vec3 emissionAt(uint primitive,vec2 bary,bool accepted,vec3 start){assert(!accepted);return vec3(float(primitive+1)*bary.x,bary.y,abs(start.x)*.01f);}
static void addDirect(vec3 brdf,vec3 light){contributions.push_back(brdf);contributions.push_back(diffuseBRDF);contributions.push_back(light);}
#define PT_CATEGORY(x) ((void)0)
static void original(vec3 start,vec3 n,vec3 geometric,vec3 v,vec3 albedo,float roughness,float metallic,int bounce){
#include "pt_light_loop_original.inc"
}
static void compact(vec3 start,vec3 n,vec3 geometric,vec3 v,vec3 albedo,float roughness,float metallic,int bounce){
#include "pt_light_loop_compact.inc"
}
static vec3 inputVector(){return {randomFloat()*20-10,randomFloat()*20-10,randomFloat()*20-10};}
static uint roundedValues;
static float worstRelative, worstRayAbsolute, worstContributionAbsolute;
template<class T> static void same(const std::vector<T> &a,const std::vector<T> &b,const char *name){
    assert(a.size()==b.size());
    for(size_t i=0;i<a.size()*sizeof(T);i+=sizeof(float)) {
        float x,y;std::memcpy(&x,(const char *)a.data()+i,4);std::memcpy(&y,(const char *)b.data()+i,4);
        if(!std::memcmp(&x,&y,4)) continue;
        float error=std::abs(x-y),relative=error/std::max(std::abs(y),1e-6f);
        ++roundedValues;worstRelative=std::max(worstRelative,relative);
        if(!std::strcmp(name,"visibility rays")) worstRayAbsolute=std::max(worstRayAbsolute,error);
        else worstContributionAbsolute=std::max(worstContributionAbsolute,error);
#ifdef __FAST_MATH__
        // Strict mode above requires bit identity. Fast-math may replace
        // division with reciprocals and vectorize the common site differently.
        // Keep ray count/order and RNG exact, with a tighter numeric tolerance
        // for ray coordinates than for accumulated radiance.
        float tolerance=std::strcmp(name,"visibility rays") ? 1e-4f:2e-6f;
        // Cone samples can graze the shading horizon: near-zero dot products
        // amplify relative error from fast-math basis/trigonometric rounding.
        // Keep the old delta-light limit and use a bounded absolute allowance
        // only for finite-source contributions; strict mode remains bit exact.
        bool finiteSource=skyEnvironment.xyz.z>0 || skyEnvironment.w>0;
        float absolute=std::strcmp(name,"visibility rays") ? (finiteSource ? 1e-6f:1e-7f):1e-6f;
        if(error<=absolute+std::abs(y)*tolerance) continue;
#endif
        std::fprintf(stderr,"trial=%u %s differ at float=%zu x=%.9g y=%.9g rel=%.9g\n",trial,name,i/4,x,y,relative);std::abort();
    }
}
int main(){
    uint sceneSeed=0x17b53689u;uint64_t rayCount=0,contributionCount=0;
    for(trial=0;trial<100000;++trial){
        rng=sceneSeed;
        lightCounts={trial%65,trial%33,trial%1025,trial%10};
        if(!lightCounts.z) lightCounts.w=0;
        ptEmitterGeometry=(trial&1)!=0;ptMapLightCull=(trial&2)!=0;
        skyEnvironment={vec3(0,0,trial%3 ? 0:.08f),trial%5 ? 0:4.0f};
        pc.sunExposure={normalize(inputVector()),0};pc.sunRadiance={trial%7 ? vec3(4,1,2):vec3(0),0};
        pc.parameters={vec3(0,100,0),0};pc.sampling.y=float(1+trial%8);
        lightSelection.x=100;
        for(auto &p:positions) p=inputVector();
        for(uint i=0;i<1056;++i){pointPositions[i]={inputVector(),randomFloat()*100};pointColors[i]={inputVector()+10,0};}
        vec3 start=inputVector(),n=normalize(inputVector()),geometric=trial%3 ? n:normalize(inputVector());
        vec3 v=normalize(inputVector()),albedo=(inputVector()+10)/20;
        float roughness=randomFloat(),metallic=randomFloat();int bounce=int(trial%8);
        if(trial%13==0) for(auto &p:pointPositions) p.xyz=start; // Near-source rejections.
        uint seed=rng;
        rays.clear();contributions.clear();diffuseBRDF=vec3(123);
        original(start,n,geometric,v,albedo,roughness,metallic,bounce);
        uint after=rng;auto expectedRays=rays;auto expectedContributions=contributions;vec3 expectedDiffuse=diffuseBRDF;
        rng=seed;rays.clear();contributions.clear();diffuseBRDF=vec3(123);
        compact(start,n,geometric,v,albedo,roughness,metallic,bounce);
        assert(rng==after);
        same(std::vector<vec3>{diffuseBRDF},std::vector<vec3>{expectedDiffuse},"final diffuse BRDF");
        same(rays,expectedRays,"visibility rays");same(contributions,expectedContributions,"direct lighting");
        rayCount+=rays.size()/2;contributionCount+=contributions.size()/3;sceneSeed=after;
    }
    std::printf("PASS: %u scenes, %llu matching shadow rays, %llu matching split-BRDF/light contributions; exact RNG/count/order\n",trial,(unsigned long long)rayCount,(unsigned long long)contributionCount);
    std::printf("Float rounding: %u values differ, maximum relative %.9g, absolute ray %.9g, absolute contribution %.9g\n",roundedValues,worstRelative,worstRayAbsolute,worstContributionAbsolute);
}
