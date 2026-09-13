// Execute the production GLSL appearance functions with simple CPU vectors.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
using uint = uint32_t;
using std::abs;
using std::exp;
using std::pow;
using std::floor;
static float min(float a,float b){return std::min(a,b);}
static float max(float a,float b){return std::max(a,b);}
static float smoothstep(float a,float b,float x){
    assert(a<b); float t=std::clamp((x-a)/(b-a),0.f,1.f); return t*t*(3-2*t);
}
struct vec2 {float x,y; vec2(float x,float y):x(x),y(y){} };
static vec2 operator-(vec2 a,vec2 b){return {a.x-b.x,a.y-b.y};}
static float length(vec2 a){return std::sqrt(a.x*a.x+a.y*a.y);}
struct vec3 {float x,y,z; vec3(float x,float y,float z):x(x),y(y),z(z){} };
static float dot(vec3 a,vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static vec3 operator*(vec3 a,float b){return {a.x*b,a.y*b,a.z*b};}
static vec3 mix(vec3 a,vec3 b,float t){return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};}
#include "post_nv_look.glsl"
int main(){
    for(vec3 primary : {vec3(1,0,0),vec3(0,1,0),vec3(0,0,1)})
        assert(nvLuminance(primary)>0 && nvSignal(nvLuminance(primary),1.6f)>0);
    for(float gain : {0.f,.1f,1.f,1.6f,8.f}) {
        float previous=-1;
        for(int i=0;i<=4096;++i) {
            float v=nvSignal(i/4096.f,gain);
            assert(std::isfinite(v) && v>=previous && v>=0 && v<=1);
            if(gain==0 || i==0) assert(v==0);
            previous=v;
            for(uint tint : {0u,1u}) {
                vec3 c=nvPhosphor(v,tint);
                assert(c.x>=0 && c.y>=c.x && c.y>=c.z && c.y<=1);
            }
        }
    }
    assert(nvSignal(.9f,1.6f)<nvSignal(1.f,1.6f));
    assert(nvPhosphor(.5f,1).x>nvPhosphor(.5f,0).x);
    puts("PASS: all RGB primaries visible; monotonic brightness, highlight headroom, zero gain and both phosphor modes");
    for(float aspect : {.5625f,1.f,4.f/3,16.f/9,21.f/9,32.f/9}) {
        float span=min(aspect,2.f);
        for(int y=0;y<=100;++y) for(int x=0;x<=200;++x) {
            vec2 q(x/100.f*aspect-aspect,y/50.f-1);
            float m=nvLensMask(q,aspect);
            assert(std::isfinite(m) && m>=0 && m<=1);
            assert(std::abs(m-nvLensMask(vec2(-q.x,q.y),aspect))<1e-6);
            assert(std::abs(m-nvLensMask(vec2(q.x,-q.y),aspect))<1e-6);
        }
        assert(nvLensMask(vec2(0,0),aspect)>.9f);
        assert(nvLensMask(vec2(0,1),aspect)==0);
        for(int x=-100;x<=100;++x) assert(nvLensMask(vec2(x/100.f*.66f*span,0),aspect)>.5f);
    }
    puts("PASS: centred symmetric soft optics, unobstructed middle and connected lenses from portrait through 32:9; no reversed smoothstep edges");
    double sum=0,squares=0,changed=0;
    for(int y=0;y<512;++y) for(int x=0;x<512;++x) {
        float n=nvNoise(vec2(float(x),float(y)),.25f);
        assert(n>=-1 && n<=1 && n==nvNoise(vec2(float(x),float(y)),.25f));
        sum+=n; squares+=n*n;
        changed+=n!=nvNoise(vec2(float(x),float(y)),.5f);
    }
    assert(std::abs(sum/262144)<.005 && squares/262144>.1 && changed/262144>.99);
    printf("PASS: deterministic animated grain, mean %.6f (no systematic darkening)\n",sum/262144);
}
