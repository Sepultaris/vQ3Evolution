// Run the actual shader modifier loops with scalar/vector arithmetic on CPU.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
struct vec2 { float x,y; vec2(float a=0):x(a),y(a){} vec2(float a,float b):x(a),y(b){} };
struct vec3 { float x,y,z; vec3(float a=0):x(a),y(a),z(a){} vec3(float a,float b,float c):x(a),y(b),z(c){} vec3(vec2 a,float b):x(a.x),y(a.y),z(b){} };
struct vec4 { float x,y,z,w; vec4(float a=0):x(a),y(a),z(a),w(a){} vec4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){} };
struct mat2 { vec2 c[2]; mat2(float a=0):c{{a,0},{0,a}}{} mat2(float a,float b,float d,float e):c{{a,b},{d,e}}{} mat2(vec2 a,vec2 b):c{a,b}{} vec2 &operator[](int i){return c[i];} };
static vec2 operator+(vec2 a,vec2 b){return {a.x+b.x,a.y+b.y};}
static vec2 operator-(vec2 a,vec2 b){return {a.x-b.x,a.y-b.y};}
static vec2 operator*(vec2 a,vec2 b){return {a.x*b.x,a.y*b.y};}
static vec2 operator/(vec2 a,vec2 b){return {a.x/b.x,a.y/b.y};}
static vec2 &operator+=(vec2 &a,vec2 b){return a=a+b;}
static vec2 &operator*=(vec2 &a,vec2 b){return a=a*b;}
static vec2 operator*(mat2 a,vec2 b){return a[0]*b.x+a[1]*b.y;}
static mat2 operator*(mat2 a,mat2 b){return {a*b[0],a*b[1]};}
static mat2 &operator/=(mat2 &a,float b){a[0]=a[0]/b;a[1]=a[1]/b;return a;}
static float dot(vec3 a,vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
// Keep GLSL's Fract intrinsic as a rounded operation. C++ fast-math must not
// reassociate uv + (x - floor(x)) into (uv + x) - floor(x): that changes the
// reference before this fixture even compares the production shader bodies.
static __attribute__((noinline)) float fract(float x){return x-std::floor(x);}
static vec2 fract(vec2 x){return {fract(x.x),fract(x.y)};}
static vec2 sin(vec2 x){return {std::sin(x.x),std::sin(x.y)};}
static vec2 cos(vec2 x){return {std::cos(x.x),std::cos(x.y)};}
static float mix(float a,float b,float t){return a*(1-t)+b*t;}
static float smoothstep(float a,float b,float x){float t=std::clamp((x-a)/(b-a),0.0f,1.0f);return t*t*(3-2*t);}
using std::abs; using std::sin; using std::cos; using std::floor;
constexpr float PI=3.141592653589793f;
struct TexMod {vec4 a,b,wave;};
static TexMod mods[4];
#include "pt_wave.inc"
static void reference(vec2 &uv,mat2 &gradients,float time,int count) {
    struct {bool uniformTint;vec3 timeScroll;} context={false,vec3(0)};
    struct {vec4 params;} layer={vec4{0,0,0,float(count)}};
    vec3 local,localDx,localDy;
#include "pt_cache_original.inc"
}
static TexMod precache(float time,int count) {
    TexMod cached;cached.a=cached.b=vec4(0);
#include "pt_cache_compact.inc"
    return cached;
}
static void cached(vec2 &uv,mat2 &gradients,float time,int count) {
    TexMod cached=precache(time,count);
#include "pt_cache_consumer.inc"
}
static uint32_t rng=729173;
static float randomFloat(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return float(rng>>8)/16777216.0f;}
static float randomSigned(){return randomFloat()*2-1;}
static float worstUV,worstGradient,trialTime;
static unsigned trialIndex;
static void same(float a,float b,bool gradient){
    assert(std::isfinite(a)&&std::isfinite(b));
    float error=std::abs(a-b),scale=std::max(std::abs(a),std::abs(b));
    if(gradient) worstGradient=std::max(worstGradient,error); else worstUV=std::max(worstUV,error);
#ifndef __FAST_MATH__
    assert(a==b);
#endif
    // Fast-math may reassociate multiply/add/divide around a near-zero stretch.
    // Strict builds above require identity; this is a bounded rounding check.
    if(error>0.00001f+scale*0.000008f){
        std::printf("mismatch %.9g %.9g error %.9g gradient %d trial %u time %.9g\n",a,b,error,gradient,trialIndex,trialTime);
        for(unsigned i=0;i<trialIndex%4;++i)std::printf("mod %u type %.0f wave %.9g %.9g %.9g %.9g function %.0f\n",i,mods[i].a.x,mods[i].wave.x,mods[i].wave.y,mods[i].wave.z,mods[i].wave.w,mods[i].b.w);
        std::fflush(stdout);assert(false);
    }
}
int main(){
    const int types[]={0,1,3,4,5,6,7};
    for(unsigned trial=0;trial<200000;++trial){
        int count=int(trial%4);
        for(int i=0;i<count;++i){
            auto &m=mods[i];
            m.a={float(types[(trial+i*3)%7]),randomSigned()*2,randomSigned()*2,randomSigned()*2};
            m.b={randomSigned()*2,randomSigned()*2,randomSigned()*2,float(1+(trial+i)%6)};
            // Includes negative and near-zero stretch as well as all wave types.
            m.wave={randomSigned()*2,randomSigned(),randomSigned()*3,randomSigned()};
            if(int(m.a.x)==6)m.a.y*=180;
        }
        vec2 a(randomSigned()*8,randomSigned()*8),b=a;
        mat2 ga(randomSigned(),randomSigned(),randomSigned(),randomSigned()),gb=ga;
        float time=randomSigned()*1000;
        trialIndex=trial;trialTime=time;
        reference(a,ga,time,count);cached(b,gb,time,count);
        same(a.x,b.x,false);same(a.y,b.y,false);
        for(int i=0;i<2;++i){same(ga[i].x,gb[i].x,true);same(ga[i].y,gb[i].y,true);}
    }
    std::printf("PASS: 200000 production modifier chains; UV/anisotropic derivative equivalence (max abs %.9g / %.9g)\n",worstUV,worstGradient);
}
