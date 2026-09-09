// Execute both production GLSL transport helpers, without substituting a model
// of the candidate. Floating-point reassociation permits bounded roundoff.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
using uint=uint32_t;
struct vec3 { float x,y,z; vec3(float v=0):x(v),y(v),z(v){} vec3(float a,float b,float c):x(a),y(b),z(c){} };
static vec3 operator+(vec3 a,vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
static vec3 operator-(vec3 a,vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
static vec3 operator*(vec3 a,vec3 b){return {a.x*b.x,a.y*b.y,a.z*b.z};}
static vec3 &operator+=(vec3 &a,vec3 b){return a=a+b;}
static vec3 &operator*=(vec3 &a,vec3 b){return a=a*b;}
static vec3 max(vec3 a,vec3 b){return {std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)};}
static float clamp(float a,float lo,float hi){return std::clamp(a,lo,hi);}
namespace reference {
vec3 radianceD,radianceS,radianceT,radianceE,diffuseBRDF;
#include "../code/renderer_vulkan/shaders/pt_compact_transport.glsl"
#include "../code/renderer_vulkan/shaders/pt_ambient.glsl"
vec3 total(){return radianceD+radianceS+radianceT+radianceE;}
}
namespace combined {
vec3 diffuseBRDF;
#include "../code/renderer_vulkan/shaders/pt_rr_transport.glsl"
#include "../code/renderer_vulkan/shaders/pt_ambient.glsl"
}
static uint rng=97123;
static float randomValue(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return float(rng>>8)/16777216.f;}
static vec3 color(){return {randomValue(),randomValue(),randomValue()};}
static float worstRelative=0;
static void same(vec3 a,vec3 b){
    float x[]={a.x,a.y,a.z},y[]={b.x,b.y,b.z};
    for(int c=0;c<3;++c){
        assert(std::isfinite(x[c]) && std::isfinite(y[c]));
        float scale=std::max(std::abs(x[c]),std::abs(y[c]));
        float error=std::abs(x[c]-y[c]);
        worstRelative=std::max(worstRelative,error/std::max(scale,1.f));
        assert(error<=2e-6f+scale*1e-5f);
    }
}
int main(){
    for(uint path=0;path<100000;++path){
        reference::resetCompactPath(); combined::resetCompactPath();
        for(uint event=0;event<32;++event){
            vec3 diffuse=color(),brdf=color()*2,light=color()*16,value=color()*1.5f;
            reference::diffuseBRDF=combined::diffuseBRDF=diffuse;
            switch(uint(randomValue()*8)){
            case 0: reference::addIncident(light); combined::addIncident(light); break;
            case 1: reference::addDirect(brdf,light); combined::addDirect(brdf,light); break;
            case 2: reference::attenuate(value); combined::attenuate(value); break;
            case 3: reference::compactReflect(); combined::compactReflect(); break;
            case 4: reference::compactTransmit(); combined::compactTransmit(); break;
            case 5: {
                float factor=randomValue()*2;
                reference::compactScatter(brdf,factor); combined::compactScatter(brdf,factor); break;
            }
            case 6: { // Surviving volume scatter keeps prior reflection/transmission class.
                reference::attenuate(value); combined::attenuate(value);
                if(reference::pathClass==0u) reference::pathClass=1u;
                if(combined::pathClass==0u) combined::pathClass=1u;
                reference::addIncident(light); combined::addIncident(light); break;
            }
            case 7: {
                float metal=randomValue()*1.4f-.2f,strength=randomValue()*2;
                reference::addAmbient(value,metal,strength); combined::addAmbient(value,metal,strength); break;
            }
            }
            same(reference::total(),combined::rrRadiance);
            same(reference::compactThroughput(),combined::compactThroughput());
            assert(reference::pathClass==combined::pathClass);
        }
    }
    std::printf("PASS: 3.2 million actual GLSL transport events; radiance/roulette/fog/ambient, max scaled error %.9g\n",worstRelative);
}
