/* Runs original and cached production GLSL on CPU, including split diffuse
 * output, PDF, continuation direction and random draw ordering. */
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using uint=uint32_t;
struct vec3 {
    union { float x,r; }; union { float y,g; }; union { float z,b; };
    vec3(float a=0):x(a),y(a),z(a){}
    vec3(float x,float y,float z):x(x),y(y),z(z){}
};
struct vec4 {
    float x,y,z,w;
    vec4(float x,float y,float z,float w):x(x),y(y),z(z),w(w){}
};
static vec3 operator+(vec3 a,vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
static vec3 operator-(vec3 a,vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
static vec3 operator-(vec3 a){return {-a.x,-a.y,-a.z};}
static vec3 operator*(vec3 a,vec3 b){return {a.x*b.x,a.y*b.y,a.z*b.z};}
static vec3 operator/(vec3 a,vec3 b){return {a.x/b.x,a.y/b.y,a.z/b.z};}
static float dot(vec3 a,vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static vec3 cross(vec3 a,vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static vec3 normalize(vec3 a){return a/std::sqrt(dot(a,a));}
static vec3 reflect(vec3 i,vec3 n){return i-2*dot(n,i)*n;}
static float max(float a,float b){return std::max(a,b);}
static float min(float a,float b){return std::min(a,b);}
using std::abs;
static float clamp(float x,float lo,float hi){return min(max(x,lo),hi);}
static float mix(float a,float b,float t){return a*(1-t)+b*t;}
static vec3 mix(vec3 a,vec3 b,float t){return a*(1-t)+b*t;}
static float pow(float a,int b){return std::pow(a,float(b));}
static uint64_t squareRoots=0;
static float sqrt(float a){++squareRoots;return std::sqrt(a);}
static float sin(float a){return std::sin(a);}
static float cos(float a){return std::cos(a);}
static const float PI=3.141592653589793f;
static vec3 diffuseBRDF;
static float draws[3]; static uint drawIndex;
static float randomFloat(){assert(drawIndex<3);return draws[drawIndex++];}
#include "pt_original_brdf.inc"
#define PT_BRDF_REUSE 1
#include "pt_brdf_reuse.inc"

static uint state=0x61879fabu;
static float randomInput(){state^=state<<13;state^=state>>17;state^=state<<5;return float(state>>8)*(1.0f/16777216.0f);}
static vec3 direction(){float z=randomInput()*2-1,phi=2*PI*randomInput(),r=std::sqrt(max(0,1-z*z));return {r*std::cos(phi),r*std::sin(phi),z};}
static uint bits(float value){uint u;std::memcpy(&u,&value,4);return u;}
static uint64_t valuesChecked=0,bitwiseEqual=0;
static float worstRelative=0;
static int compareKind=0;
static float worstByKind[4]={0},absoluteByKind[4]={0};
static uint64_t failures=0;
static void near(float original,float cached){
    ++valuesChecked;
    assert((bits(original)&0x7f800000u)!=0x7f800000u && (bits(cached)&0x7f800000u)!=0x7f800000u);
    if(bits(original)==bits(cached)){++bitwiseEqual;return;}
    float error=std::abs(original-cached);
    float relative=error/max(std::abs(original),1e-6f);
    worstRelative=max(worstRelative,relative);
    worstByKind[compareKind]=max(worstByKind[compareKind],relative);
    absoluteByKind[compareKind]=max(absoluteByKind[compareKind],error);
    // Strict compilation requires bit identity. Reassociation/reciprocal
    // optimization gets a 0.01% relative budget for radiance/PDF (plus 1e-7
    // near zero), but NEVER for stochastic continuation directions/branches.
#ifdef __FAST_MATH__
    bool acceptable=compareKind!=3 && error<=1e-7f+std::abs(original)*1e-4f;
#else
    bool acceptable=false;
#endif
    if(!acceptable){
        ++failures;
    }
}
static void near(vec3 a,vec3 b){near(a.x,b.x);near(a.y,b.y);near(a.z,b.z);}
static uint64_t evaluations=0,samples=0;
static void checkView(vec3 n,vec3 v,vec3 albedo,float roughness,float metallic){
    vec4 view=prepareBRDFView(n,v,albedo,roughness,metallic);
    // Reuse one hit across light directions, then construct a fresh next hit.
    for(int i=0;i<8;++i){
        vec3 l=i==0 ? n : (i==1 ? -n:direction());
        float originalPDF=-1,cachedPDF=-1;
        diffuseBRDF=vec3(123);
        vec3 original=evaluateBRDF(n,v,l,albedo,roughness,metallic,originalPDF);
        vec3 originalDiffuse=diffuseBRDF;
        diffuseBRDF=vec3(321);
        vec3 cached=evaluateCachedBRDF(n,v,l,albedo,metallic,view,cachedPDF);
        compareKind=0;near(original,cached);
        compareKind=1;near(originalDiffuse,diffuseBRDF);
        compareKind=2;near(originalPDF,cachedPDF);
        ++evaluations;
    }
    for(int i=0;i<3;++i){
        draws[0]=randomInput();draws[1]=i==0 ? 0 : (i==1 ? std::nextafter(1.0f,0.0f):randomInput());
        // Include branch boundaries to catch a changed sampling mixture/RNG.
        draws[2]=i==0 ? view.z : (i==1 ? std::nextafter(view.z,0.0f):randomInput());
        drawIndex=0;vec3 original=sampleBRDF(n,v,albedo,roughness,metallic);assert(drawIndex==3);
        drawIndex=0;vec3 cached=sampleCachedBRDF(n,v,view);assert(drawIndex==3);
        compareKind=3;near(original,cached);++samples;
    }
}
int main(){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    for(float roughness:{0.02f,0.03162277f,0.1f,0.5f,1.0f})
    for(float metallic:{0.0f,0.25f,0.5f,0.99f,1.0f})
    for(vec3 albedo:{vec3(0),vec3(0.98f),vec3(0.98f,0.01f,0.35f)})
    for(float nv:{-1.0f,-0.01f,0.0f,1e-8f,0.001f,0.1f,0.7f,1.0f})
        checkView(vec3(0,0,1),vec3(std::sqrt(max(0,1-nv*nv)),0,nv),albedo,roughness,metallic);
    for(int i=0;i<60000;++i)
        checkView(direction(),direction(),vec3(randomInput()*.98f,randomInput()*.98f,randomInput()*.98f),
            .02f+randomInput()*.98f,randomInput());
    // Count the removed view-side square roots in an eight-visible-light hit.
    float pdf; vec3 n(0,0,1),v(0,0,1),albedo(.5f);
    squareRoots=0;
    for(int i=0;i<8;++i)evaluateBRDF(n,v,n,albedo,.5f,.1f,pdf);
    uint64_t originalRoots=squareRoots;
    squareRoots=0;vec4 view=prepareBRDFView(n,v,albedo,.5f,.1f);
    for(int i=0;i<8;++i)evaluateCachedBRDF(n,v,n,albedo,.1f,view,pdf);
    assert(originalRoots==16 && squareRoots==9);
    for(int i=0;i<4;++i)std::printf("Math category %d: maximum relative %.9g absolute %.9g\n",i,worstByKind[i],absoluteByKind[i]);
    if(failures){std::printf("FAIL: %llu values exceed the numeric/branch contract\n",(unsigned long long)failures);std::abort();}
    std::printf("PASS: %llu BRDF evaluations and %llu continuation samples; split diffuse/PDF, edge cases, fresh hits and three-draw order\n",
        (unsigned long long)evaluations,(unsigned long long)samples);
    std::printf("Scalar results: %llu/%llu bit-identical; max relative difference %.9g; eight-light Smith roots 16 -> 9 (not GPU timing)\n",
        (unsigned long long)bitwiseEqual,(unsigned long long)valuesChecked,worstRelative);
}
