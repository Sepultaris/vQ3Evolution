#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
using uint=uint32_t;
struct vec2 {float x,y;};
struct vec3 {float x,y,z;vec3(float a,float b,float c):x(a),y(b),z(c){}};
struct vec4 {float x,y,z,w;};
struct {vec4 cameraMedium;} rp;
struct Material {vec4 optical,absorption;};
static Material materials[1024];
struct Vertex {vec4 local;};
static Vertex vertices[8192];
struct uvec3 {uint x,y,z;};
static uvec3 triangle(uint p){return {p,0,0};}
#include "pt_medium_records.inc"
struct Candidate {uint primitive;vec2 bary;};
struct Collection {uint primitives[16];vec2 barycentrics[16];int count;bool touched;};
#include "pt_decal_reference.inc"
#include "pt_decal_records.inc"
static uint rng=52817;
static uint next(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static float value(){return float(next()>>8)/16777216.0f;}
static bool equal(vec3 a,vec3 b){return a.x==b.x && a.y==b.y && a.z==b.z;}
int main(){
    for(auto &m:materials) {m.optical={1+value()*2,0,0,0};m.absorption={value(),value(),value(),0};}
    for(uint i=0;i<8192;++i) vertices[i].local.w=float(i/4); // fan-edge duplicate polygons
    for(uint path=0;path<100000;++path) {
        rp.cameraMedium={1+value(),value(),value(),value()};
        uint records[4];float ior[4];vec3 absorption[4]={{0,0,0},{0,0,0},{0,0,0},{0,0,0}};int count=0;
        if(path&1) {records[0]=0xffffffffu;ior[0]=rp.cameraMedium.x;absorption[0]={rp.cameraMedium.y,rp.cameraMedium.z,rp.cameraMedium.w};count=1;}
        for(uint step=0;step<64;++step) {
            uint id=next()%1024;
            if((next()&3)!=0 && count<4) {
                records[count]=id;ior[count]=materials[id].optical.x;
                absorption[count]={materials[id].absorption.x,materials[id].absorption.y,materials[id].absorption.z};++count;
            } else if(count) --count;
            for(int i=0;i<count;++i) {
                assert(mediumRecordIOR(records[i])==ior[i]);
                assert(equal(mediumRecordAbsorption(records[i]),absorption[i]));
            }
        }
        std::vector<Candidate> hits;
        uint n=path%97;
        for(uint i=0;i<n;++i) {uint p=next()%8192;hits.push_back({p,{value(),value()}});if(i%7==0) hits.push_back({(p/4)*4+next()%4,{value(),value()}});}
        Collection a=reference(hits),b=compact(hits);
        assert(a.count==b.count && a.touched==b.touched);
        for(int i=0;i<a.count;++i) {
            assert(a.primitives[i]==b.primitives[i]);
            assert(a.barycentrics[i].x==b.barycentrics[i].x && a.barycentrics[i].y==b.barycentrics[i].y);
        }
    }
    std::puts("PASS: 6.4 million medium-stack events and 100,000 production decal collections; exact optical values, order, deduplication, barycentrics and 16-layer retention");
}
