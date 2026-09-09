// Actual native packing and production GLSL, no Vulkan device or GPU workload.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
using uint=uint32_t;
struct vec2 { float x,y; };
struct vec3 { float x,y,z; vec3(float v=0):x(v),y(v),z(v){} vec3(float x,float y,float z):x(x),y(y),z(z){} };
struct vec4 { vec3 xyz; float w; };
struct uvec3 { uint x,y,z; };
struct uvec4 { uint x,y,z,w; };
static vec3 operator+(vec3 a,vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
static vec3 operator-(vec3 a,vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
static vec3 operator*(vec3 a,float b){return {a.x*b,a.y*b,a.z*b};}
static vec3 operator/(vec3 a,float b){return {a.x/b,a.y/b,a.z/b};}
static vec3 cross(vec3 a,vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static float length(vec3 v){return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);}
static float max(float a,float b){return std::max(a,b);}
static vec3 positions[64*3];
static uint triangleMaterials[64];
static struct { struct {float x,y,z,w;} emission; } materials[64];
static struct { vec4 a,b,c; } emitterGeometry[64];
static bool ptEmitterGeometry;
static unsigned gathers;
static uvec3 triangle(uint i){++gathers;return {i*3,i*3+1,i*3+2};}
static vec3 position(uint i){++gathers;return positions[i];}
#include "pt_emitter_geometry.h"
#include "pt_emitter_geometry.inc"
static uint state=0x401926bdu;
static float randomInput(){state^=state<<13;state^=state>>17;state^=state<<5;return float(state>>8)*(1.0f/16777216.0f);}
static unsigned compareKind, differences[3];
static float worstAbsolute[3],worstRelative[3];
static void same(float a,float b){
    assert(std::isfinite(a) && std::isfinite(b));
    if(std::memcmp(&a,&b,4)==0) return;
    float error=std::abs(a-b),relative=error/max(std::abs(a),1e-6f);
    ++differences[compareKind];worstAbsolute[compareKind]=max(worstAbsolute[compareKind],error);
    worstRelative[compareKind]=max(worstRelative[compareKind],relative);
    if(differences[compareKind]<=3) std::printf("difference kind=%u a=%.9g b=%.9g absolute=%.9g relative=%.9g\n",compareKind,a,b,error,relative);
}
static void same(vec3 a,vec3 b){same(a.x,b.x);same(a.y,b.y);same(a.z,b.z);}
int main(){
    static_assert(sizeof(pt_emitter_geometry_t)==48 && sizeof(emitterGeometry[0])==48,"std430");
    for(uint trial=0;trial<300000;++trial){
        uint primitive=trial%64,slot=(trial*17)%64;
        for(unsigned v=0;v<3;++v) positions[primitive*3+v]={randomInput()*100000-50000,randomInput()*100000-50000,randomInput()*100000-50000};
        if(trial%17==0) positions[primitive*3+2]=positions[primitive*3+1]; // Degenerate.
        if(trial%19==0) for(unsigned v=0;v<3;++v) positions[primitive*3+v].z=0; // Planar world.
        triangleMaterials[primitive]=primitive|0x10000000u;
        float power=trial%13==0 ? 0:randomInput()*100000;
        materials[primitive].emission.y=power;
        pt_emitter_geometry_t packed;
        cache_emitter_geometry(&packed,&positions[primitive*3].x,&positions[primitive*3+1].x,&positions[primitive*3+2].x,power);
        std::memcpy(&emitterGeometry[slot],&packed,48);
        float root=trial%5==0 ? 0:(trial%7==0 ? 1:std::sqrt(randomInput()));
        vec2 bary={root*(1-randomInput()),0};bary.y=root-bary.x;
        vec3 target[2],normal[2];float powers[2];
        for(unsigned mode=0;mode<2;++mode){
            ptEmitterGeometry=mode!=0;gathers=0;
            sampledEmitterGeometry(primitive,slot,root,bary,target[mode],normal[mode],powers[mode]);
            assert(gathers==(mode ? 0:4));
        }
        compareKind=0;same(target[0],target[1]);same(normal[0],normal[1]);same(powers[0],powers[1]);
        // Keep the original target, geometric-normal and PDF arithmetic.
        vec3 a=positions[primitive*3],b=positions[primitive*3+1],c=positions[primitive*3+2];
        vec3 original=a*(1-root)+b*bary.x+c*bary.y;
        vec3 edges=cross(b-a,c-a);
        compareKind=1;same(original,target[1]);same(edges/max(length(edges),0.000001),normal[1]);
        float distanceSquared=randomInput()*1000000,cosine=randomInput(),total=1+randomInput()*1000000;
        compareKind=2;same(power*distanceSquared/max(total*cosine,0.000001),powers[1]*distanceSquared/max(total*cosine,0.000001));
    }
    for(unsigned kind=0;kind<3;++kind) std::printf("kind=%u differences=%u max_absolute=%.9g max_relative=%.9g\n",kind,differences[kind],worstAbsolute[kind],worstRelative[kind]);
    assert(differences[0]==0 && differences[1]==0 && differences[2]==0);
    puts("PASS: 300000 animated/planar/degenerate emitter cases; bit-identical targets, normals, power and PDFs; dependent geometry gathers 4 -> 0");
}
