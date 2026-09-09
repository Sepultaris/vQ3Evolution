// Execute the actual GLSL interval/transmittance/free-flight functions on CPU.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>
using uint=unsigned;
using std::abs; using std::exp; using std::log;
static float min(float a,float b){return std::min(a,b);}
static float max(float a,float b){return std::max(a,b);}
struct vec2 { float x=0,y=0; vec2()=default; vec2(float a,float b):x(a),y(b){} };
struct vec3 { float x=0,y=0,z=0; vec3()=default; vec3(float a,float b,float c):x(a),y(b),z(c){} float operator[](int i)const{return (&x)[i];} };
static vec3 operator/(vec3 v,float s){return {v.x/s,v.y/s,v.z/s};}
struct vec4 { float x=0,y=0,z=0,w=0; vec3 xyz()const{return {x,y,z};} float operator[](int i)const{return (&x)[i];} };
struct uvec4 { uint x=0,y=0,z=0,w=0; };
struct FogVolume {vec4 mins,maxs,albedo,emission;uvec4 planes;};
static FogVolume fogVolumes[256];
static vec4 fogPlanes[64];
static uvec4 fogControl;
static vec4 fogMins,fogMaxs;
using qboolean=bool;
constexpr bool qtrue=true,qfalse=false;
struct fog_t {float bounds[2][3];};
struct dplane_t {float normal[3],dist;};
static float LittleFloat(float f){return f;}
#include "pt_fog_bounds.inc"
static float dot(vec3 a,vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static std::mt19937 generator(53117);
static unsigned draws;
static float fixedRandom=-1;
static float randomFloat(){++draws;return fixedRandom>=0?fixedRandom:float(generator()>>8)*(1.f/16777216.f);}
#define PT_COUNT(category) ((void)0)
#include "pt_fog_volume.inc"
struct Hit {float distance;};
static struct {vec4 parameters,originNear;} pc={{0,100,0,0},{0,0,0,0.1f}};
static uvec4 lightCounts;
static float worldDistance,weaponDistance,lastWorldMaximum;
static unsigned worldQueries,weaponQueries;
static bool trace(vec3,vec3,float lo,float hi,uint purpose,Hit &hit){
    assert(lo<hi);
    float distance;
    if(purpose==3){++weaponQueries;distance=weaponDistance;}
    else {++worldQueries;lastWorldMaximum=hi;distance=worldDistance;}
    if(distance>=lo && distance<=hi){hit.distance=distance;return true;}
    return false;
}
static float continuationT(float t){return std::nextafter(t,INFINITY);}
#include "pt_fog_primary.inc"
#include "pt_fog_query.inc"
static void close(float a,float b){assert(std::abs(a-b)<1e-5f);}
static void reset(){std::fill(fogVolumes,fogVolumes+256,FogVolume{});fogControl={};draws=0;fogMins={1e30f,1e30f,1e30f,0};fogMaxs={-1e30f,-1e30f,-1e30f,0};}
static void box(uint i,float lo,float hi,float density){
    fogVolumes[i].mins={lo,-1,-1,density};fogVolumes[i].maxs={hi,1,1,0};
    fogControl.x=std::max(fogControl.x,i+1);
    fogMins={min(fogMins.x,lo),-1,-1,0};fogMaxs={max(fogMaxs.x,hi),1,1,0};
}
int main(){
    fog_t brush={{{-2,-3,-4},{5,6,7}}};
    for(int axis=0;axis<3;++axis) for(int sign=-1;sign<=1;sign+=2) {
        dplane_t plane={};plane.normal[axis]=float(sign);plane.dist=sign*brush.bounds[sign>0][axis];
        assert(fog_bound_plane(&brush,&plane));
        plane.dist-=.01f;assert(!fog_bound_plane(&brush,&plane));
        plane.dist+=.02f;assert(!fog_bound_plane(&brush,&plane));
        plane.normal[(axis+1)%3]=.001f;assert(!fog_bound_plane(&brush,&plane));
    }
    // Optimized slabs must match the original halfspace clip, including zero
    // directions, boundary origins, negative rays and finite path segments.
    for(int i=0;i<100000;++i) {
        vec3 o={randomFloat()*12-6,randomFloat()*12-6,randomFloat()*12-6};
        vec3 d={randomFloat()*2-1,randomFloat()*2-1,randomFloat()*2-1};
        if(i%3==0)d.x=0; if(i%5==0)d.y=-0.f; if(i%7==0)o.z=3;
        float a=randomFloat(),b=a+randomFloat()*30,c=a,e=b;
        bool ref=true;
        for(int k=0;k<3 && ref;++k)ref=fogClip(-3-o[k],-d[k],a,b) && fogClip(o[k]-3,d[k],a,b);
        bool fast=fogBounds({-3,-3,-3},{3,3,3},o,d,c,e);
        assert(ref==fast);if(ref){assert(abs(a-c)<1e-4 && abs(b-e)<1e-4);}
    }
    puts("PASS: native exact-bound plane deduplication and 100000 slab/reference equivalence cases");
    reset();float t;uint id;vec2 interval;
    assert(!fogSample({0,0,0},{1,0,0},0,100,t,id));
    close(fogTransmittance({0,0,0},{1,0,0},0,100),1);assert(draws==0);
    box(0,2,6,.2f);
    assert(fogInterval(0,{0,0,0},{1,0,0},0,100,interval));close(interval.x,2);close(interval.y,6);
    close(fogTransmittance({0,0,0},{1,0,0},0,100),std::exp(-.8f));
    assert(fogInterval(0,{4,0,0},{1,0,0},0,100,interval));close(interval.x,0);close(interval.y,2);
    assert(!fogInterval(0,{0,2,0},{1,0,0},0,100,interval));
    draws=0; assert(!fogSample({0,2,0},{1,0,0},0,100,t,id));assert(draws==0);
    close(fogTransmittance({0,2,0},{1,0,0},0,100),1);
    assert(!fogInterval(0,{0,0,0},{1,0,0},0,1,interval));
    assert(!fogInterval(0,{0,0,0},{1,0,0},5,5,interval));
    close(fogTransmittance({0,0,0},{1,0,0},3,5),std::exp(-.4f));
    // Diagonal cut x+y<=4, retaining the brush rather than only its bounds.
    fogVolumes[0].planes={0,1,0,0};fogPlanes[0]={1,1,0,4};
    assert(fogInterval(0,{0,.5f,0},{1,0,0},0,100,interval));close(interval.y,3.5f);
    puts("PASS: actual GLSL convex bounds, oblique plane, inside/outside, parallel and truncated segments");
    fogVolumes[0].planes.y=0;
    box(1,4,8,.3f);
    close(fogTransmittance({0,0,0},{1,0,0},0,100),std::exp(-2.f));
    close(fogTransmittance({0,0,0},{1,0,0},0,5)*fogTransmittance({0,0,0},{1,0,0},5,100),std::exp(-2.f));
    // Independent reference integral for first collision in either overlapping medium.
    const double a=1-std::exp(-.4), b=std::exp(-.4)*(1-std::exp(-1.));
    const double p0=a+b*.4, p1=b*.6+std::exp(-1.4)*(1-std::exp(-.6));
    unsigned hits[2]={0,0},misses=0;const unsigned n=500000;
    for(unsigned i=0;i<n;++i){if(fogSample({0,0,0},{1,0,0},0,100,t,id)){assert(id<2 && t>=2 && t<=8);++hits[id];}else ++misses;}
    assert(std::abs(double(hits[0])/n-p0)<.004 && std::abs(double(hits[1])/n-p1)<.004);
    assert(std::abs(double(misses)/n-std::exp(-2.))<.004);
    puts("PASS: 500000 actual free-flight samples match analytic overlapping-media survival and collision identity");
    // Constant source along an absorbing medium: collision emission j/sigma.
    reset();box(0,0,10,.1f);double source=0,back=0;
    for(unsigned i=0;i<n;++i){if(fogSample({0,0,0},{1,0,0},0,10,t,id)) source+=.4;else back+=2;}
    assert(std::abs(source/n-.4*(1-std::exp(-1.)))<.003);
    assert(std::abs(back/n-2*std::exp(-1.))<.005);
    puts("PASS: collision emission plus surviving background matches analytic volume radiance without double extinction");
    vec3 weight;fogVolumes[0].albedo={0,0,0,0};draws=0;
    assert(!fogScatter(0,weight) && draws==0);
    fogVolumes[0].albedo={1,1,1,0};assert(fogScatter(0,weight) && draws==0);close(weight.x,1);
    fogVolumes[0].albedo={.217638f,.009423f,.00631f,0};
    double energy[3]={};unsigned surviving=0;
    for(unsigned i=0;i<n;++i)if(fogScatter(0,weight)){
        ++surviving;for(int c=0;c<3;++c)energy[c]+=weight[c];
    }
    for(int c=0;c<3;++c)assert(abs(energy[c]/n-fogVolumes[0].albedo[c])<.0015);
    assert(surviving>n*.21 && surviving<n*.23);
    puts("PASS: actual absorption/scatter selection preserves spectral energy; black/white limits do not consume RNG");
    // Real GLSL query ordering with a deterministic geometry-query mock.
    reset();Hit hit;uint medium;float flight;
    lightCounts={};worldDistance=8;weaponDistance=0;
    assert(fogPathHit({0,0,0},{1,0,0},0,0,hit,flight,medium));
    close(hit.distance,8);close(lastWorldMaximum,100);assert(draws==0);
    // Force a collision at x=5 in a box beginning at x=2.
    box(0,2,20,.2f);fixedRandom=1-std::exp(-.6f);
    assert(!fogPathHit({0,0,0},{1,0,0},0,0,hit,flight,medium));close(flight,5);close(lastWorldMaximum,flight);
    worldDistance=3;assert(fogPathHit({0,0,0},{1,0,0},0,0,hit,flight,medium));close(hit.distance,3);
    // The presentation layer has priority even if world geometry is closer.
    lightCounts.z=1;weaponDistance=9;worldQueries=weaponQueries=0;
    assert(!fogPathHit({0,0,0},{1,0,0},0,0,hit,flight,medium));
    assert(worldQueries==0 && weaponQueries==1);
    weaponDistance=4;assert(fogPathHit({0,0,0},{1,0,0},0,0,hit,flight,medium));close(hit.distance,4);
    // Secondary rays ignore first-person geometry; nearer surfaces still win.
    assert(fogPathHit({0,0,0},{1,0,0},0,1,hit,flight,medium));close(hit.distance,3);
    lightCounts.z=0;worldDistance=3.000001f;
    assert(fogPathHit({0,0,0},{1,0,0},3,0,hit,flight,medium));close(hit.distance,worldDistance);
    // A fog event before the camera near plane must not issue an inverted ray.
    pc.originNear.w=6;worldQueries=0;
    assert(!fogPathHit({0,0,0},{1,0,0},0,0,hit,flight,medium));assert(worldQueries==0);pc.originNear.w=.1f;
    // Coincident surface wins, matching the original strict t < surface test.
    worldDistance=flight;assert(fogPathHit({0,0,0},{1,0,0},0,1,hit,flight,medium));
    fixedRandom=-1;
    puts("PASS: bounded queries preserve closer surfaces, weapon priority, near plane, transparent continuation and ties");
    // Surface-first and fog-first must agree in distribution, not random draw
    // order. Put an occluded volume first to catch order-dependent bias.
    reset();box(0,12,18,.3f);box(1,2,8,.2f);worldDistance=5;
    unsigned oldEvents=0,newEvents=0;
    for(unsigned i=0;i<n;++i){
        if(fogSample({0,0,0},{1,0,0},0,worldDistance,t,id))++oldEvents;
        if(!fogPathHit({0,0,0},{1,0,0},0,1,hit,flight,medium))++newEvents;
    }
    double expected=1-std::exp(-.6);
    assert(abs(double(oldEvents)/n-expected)<.004 && abs(double(newEvents)/n-expected)<.004);
    puts("PASS: 500000 surface-first/fog-first trials preserve analytic event probability with occluded earlier records");
}
