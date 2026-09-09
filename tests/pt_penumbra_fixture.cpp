// Execute the actual GLSL helper, not a separate implementation of its formulas.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
struct vec3 {float x,y,z;vec3(float v=0):x(v),y(v),z(v){}vec3(float x,float y,float z):x(x),y(y),z(z){}};
static vec3 operator+(vec3 a,vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
static vec3 operator-(vec3 a,vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
static vec3 operator*(vec3 a,float s){return {a.x*s,a.y*s,a.z*s};}
static float dot(vec3 a,vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static vec3 cross(vec3 a,vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static float length(vec3 a){return std::sqrt(dot(a,a));}
static vec3 normalize(vec3 a){return a*(1/length(a));}
static float max(float a,float b){return std::max(a,b);}
static float sqrt(float a){return std::sqrt(a);}
static float sin(float a){return std::sin(a);}
static float cos(float a){return std::cos(a);}
static const float PI=3.14159265358979323846f;
static struct {float z,w;} skyEnvironment;
static uint32_t rng=1234567;
static float randomFloat(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return float(rng>>8)/16777216.0f;}
static vec3 basisSample(vec3 n,vec3 local){
    vec3 tangent=normalize(cross(std::abs(n.z)<.999f?vec3(0,0,1):vec3(0,1,0),n));
    return tangent*local.x+cross(n,tangent)*local.y+n*local.z;
}
#include "pt_penumbra.inc"
static double visible(float x,float z,float radius){
    skyEnvironment.w=radius;int count=0;const int samples=50000;
    for(int i=0;i<samples;++i){
        vec3 delta=vec3(0,0,100)-vec3(x,0,z),l=normalize(delta);float t;
        samplePointPenumbra(l,length(delta),t);
        float hit=(50-z)/l.z;
        assert(hit<t); // Every query actually reaches the intervening blocker.
        if(x+l.x*hit>=0)++count;
    }
    return double(count)/samples;
}
int main(){
    vec3 axis=normalize(vec3(1,2,3));float t;uint32_t initial=rng;
    vec3 l=sampleSunPenumbra(axis);assert(!std::memcmp(&l,&axis,sizeof(l)));
    assert(samplePointPenumbra(l,100,t)==1 && t==100 && rng==initial);
    assert(!std::memcmp(&l,&axis,sizeof(l)));
    for(float angle:{.000001f,.004625f,.174533f}){
        skyEnvironment.z=angle;double mean=0;
        for(int i=0;i<100000;++i){
            l=sampleSunPenumbra(axis);float cosine=dot(axis,l);
            assert(std::abs(length(l)-1)<2e-6f && cosine>=std::cos(angle)-2e-6f);
            mean+=cosine;
        }
        assert(std::abs(mean/100000-(1+std::cos(angle))*.5)<.00005);
    }
    for(float radius:{.001f,4.0f,64.0f})for(float distance:{.1f,3.0f,100.0f,10000.0f}){
        skyEnvironment.w=radius;double energy=0;
        for(int i=0;i<20000;++i){
            l=axis;float scale=samplePointPenumbra(l,distance,t);
            assert(std::isfinite(scale) && scale>0 && std::isfinite(t) && t>0);
            assert(std::abs(length(l)-1)<2e-6f);
            if(radius>.01f && distance<1000){
                vec3 endpoint=l*t-axis*distance;
                assert(std::abs(length(endpoint)-radius)<.003f);
            }
            energy+=dot(axis,l)*scale;
        }
        if(distance>radius) assert(std::abs(energy/20000-1)<.005);
    }
    assert(visible(-1,0,0)==0 && visible(1,0,0)==1);
    double dark=visible(-20,0,16),left=visible(-4,0,16),middle=visible(0,0,16),right=visible(4,0,16),lit=visible(20,0,16);
    assert(dark==0 && lit==1 && left>0 && left<middle && middle<right && right<1);
    assert(std::abs(middle-.5)<.01);
    double contact=visible(4,40,16);
    assert(contact>right+.1); // Same source; tighter shadow near blocker contact.
    double narrower=visible(4,0,4);
    assert(narrower>right+.1);
    std::printf("PASS: zero preserves directions/RNG; cone bounds/unit vectors; sphere endpoints/interior; energy normalization; blocker visibility %.3f %.3f %.3f %.3f %.3f, contact %.3f, smaller source %.3f\n",dark,left,middle,right,lit,contact,narrower);
}
