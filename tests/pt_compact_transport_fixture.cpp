#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
using uint=uint32_t;
struct vec3 {float x,y,z;vec3(float a=0):x(a),y(a),z(a){}vec3(float a,float b,float c):x(a),y(b),z(c){}};
static vec3 operator+(vec3 a,vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
static vec3 operator-(vec3 a,vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
static vec3 operator*(vec3 a,vec3 b){return {a.x*b.x,a.y*b.y,a.z*b.z};}
static vec3 &operator+=(vec3 &a,vec3 b){return a=a+b;}
static vec3 &operator*=(vec3 &a,vec3 b){return a=a*b;}
static vec3 max(vec3 a,vec3 b){return {std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)};}
static float clamp(float a,float lo,float hi){return std::clamp(a,lo,hi);}
namespace compact {
static vec3 radianceD,radianceS,radianceT,radianceE,diffuseBRDF;
#include "../code/renderer_vulkan/shaders/pt_compact_transport.glsl"
#include "../code/renderer_vulkan/shaders/pt_ambient.glsl"
}
namespace reference {
#include "pt_transport_reference.inc"
#include "../code/renderer_vulkan/shaders/pt_ambient.glsl"
static void reset(){pathD=pathS=pathT=radianceD=radianceS=radianceT=radianceE=vec3(0);pathE=vec3(1);}
static void reflect(){pathS+=pathE;pathE=vec3(0);}
static void transmit(){pathT+=pathE;pathE=vec3(0);}
static void scatter(vec3 brdf,float factor){
    pathD=(pathD*brdf+pathE*diffuseBRDF)*factor;
    pathS=(pathS*brdf+pathE*max(brdf-diffuseBRDF,vec3(0)))*factor;
    pathT*=brdf*factor;pathE=vec3(0);
}
}
static uint rng=839712;
static float random(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return float(rng>>8)/16777216.0f;}
static vec3 color(){return {random(),random(),random()};}
static float worst;
static void same(vec3 a,vec3 b){
    float x[]={a.x,a.y,a.z},y[]={b.x,b.y,b.z};
    for(int i=0;i<3;++i){
        float error=std::abs(x[i]-y[i]);worst=std::max(worst,error);
#ifndef __FAST_MATH__
        assert(x[i]==y[i]);
#endif
        if(error>0.000001f+std::max(std::abs(x[i]),std::abs(y[i]))*0.000002f)
            std::fprintf(stderr,"Transport mismatch channel %d: %.9g versus %.9g, error %.9g\n",i,x[i],y[i],error);
        assert(error<=0.000001f+std::max(std::abs(x[i]),std::abs(y[i]))*0.000002f);
    }
}
int main(){
    for(float strength:{0.f,.05f,.15f,2.f}) for(float metal:{0.f,.5f,1.f}) for(int route=0;route<3;++route) {
        reference::reset();compact::resetCompactPath();
        reference::diffuseBRDF=compact::diffuseBRDF=vec3(.123f);
        if(route==1){reference::reflect();compact::compactReflect();}
        if(route==2){reference::transmit();compact::compactTransmit();}
        vec3 albedo(.2f,.4f,.8f),attenuation(.8f,.5f,.2f);
        reference::attenuate(attenuation);compact::attenuate(attenuation);
        reference::addAmbient(albedo,metal,strength);compact::addAmbient(albedo,metal,strength);
        vec3 expected=(albedo*(.96f*(1-metal)))*strength*attenuation;
        same(reference::radianceD,compact::radianceD);same(reference::radianceS,compact::radianceS);
        same(reference::radianceT,compact::radianceT);same(reference::radianceE,vec3(0));
        // Analytic value has a different multiplication order from transport.
        vec3 actual=compact::radianceD+compact::radianceS+compact::radianceT;
        assert(std::abs(actual.x-expected.x)<1e-6f && std::abs(actual.y-expected.y)<1e-6f && std::abs(actual.z-expected.z)<1e-6f);
        if(route!=0) same(compact::radianceD,vec3(0));
        if(route!=1) same(compact::radianceS,vec3(0));
        if(route!=2) same(compact::radianceT,vec3(0));
        if(strength==0) same(compact::diffuseBRDF,vec3(.123f));
    }
    puts("PASS: actual ambient GLSL, default-off no-op, metallic suppression, diffuse/reflected/transmitted channels and attenuation");
    for(uint path=0;path<100000;++path){
        reference::reset();compact::resetCompactPath();
        for(uint step=0;step<24;++step){
            vec3 diffuse=color(),brdf=diffuse+color(),light=color()*4,value=color()*1.5f;
            reference::diffuseBRDF=compact::diffuseBRDF=diffuse;
            uint event=uint(random()*6);
            if(event==0){reference::addIncident(light);compact::addIncident(light);}
            if(event==1){reference::addDirect(brdf,light);compact::addDirect(brdf,light);}
            if(event==2){reference::attenuate(value);compact::attenuate(value);}
            if(event==3){reference::reflect();compact::compactReflect();}
            if(event==4){reference::transmit();compact::compactTransmit();}
            if(event==5){float factor=random()*2;reference::scatter(brdf,factor);compact::compactScatter(brdf,factor);}
            same(reference::radianceD,compact::radianceD);same(reference::radianceS,compact::radianceS);
            same(reference::radianceT,compact::radianceT);same(reference::radianceE,compact::radianceE);
            same(reference::pathD+reference::pathS+reference::pathT,compact::compactThroughput());
        }
    }
    std::printf("PASS: 2.4 million production transport events; four radiance channels and roulette throughput agree (max abs %.9g)\n",worst);
}
