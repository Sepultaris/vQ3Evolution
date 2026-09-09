#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
using uint=uint32_t;
using std::min;
typedef float vec3_t[3];
enum { qtrue=1, PT_MAX_MAP_LIGHTS=1024, PT_LIGHT_CELLS=512 };
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define VectorSubtract(a,b,c) do { for(uint k=0;k<3;++k) (c)[k]=(a)[k]-(b)[k]; } while(0)
#define DotProduct(a,b) ((a)[0]*(b)[0]+(a)[1]*(b)[1]+(a)[2]*(b)[2])
struct bmodel_t { float bounds[2][3]; } worldModel;
struct world_t { bmodel_t *bmodels; } testWorld={&worldModel};
struct { world_t *world; } tr={&testWorld};
struct pt_light_grid_t { float origin_cell[4]; uint dimensions[4]; float entries[PT_LIGHT_CELLS*PT_MAX_MAP_LIGHTS][4]; } grid;
struct {
    struct { pt_light_grid_t *mapped; } light_grid;
    uint map_light_count, light_cells;
    float map_positions[1024][4],map_colors[1024][4];
    int sampling_dirty;
} pt;
#include "pt_alias_pdf.h"
#include "pt_alias_builder.inc"
struct vec4 { float x,y,z,w; };
struct { uint x,y,z,w; } gridDimensions;
static uint64_t reads=0,comparisons=0;
struct AliasTable {
    vec4 operator[](uint index) const {
        assert(index<pt.light_cells*pt.map_light_count); ++reads;
        const float *p=grid.entries[index]; return {p[0],p[1],p[2],p[3]};
    }
} lightAliases;
bool ptAliasPDF=false;
float fract(float x) { return x-std::floor(x); }
#include "pt_alias_sampler.inc"
static uint state=317171;
static float uniform() {
    state^=state<<13; state^=state>>17; state^=state<<5;
    return float(state>>8)*(1.0f/16777216.0f);
}
static void compare(uint cell,float u) {
    if(u<0 || u>1) return;
    float a,b;
    ptAliasPDF=false; reads=0; uint original=proposedLight(cell,u,a);
    assert(reads==2);
    ptAliasPDF=true; reads=0; uint candidate=proposedLight(cell,u,b);
    assert(reads==1 && candidate==original && !std::memcmp(&a,&b,sizeof(a)));
    assert(b>0 && std::isfinite(b)); ++comparisons;
}
int main() {
    pt.light_grid.mapped=&grid;
    cache_alias_probabilities(nullptr,0);
    for(uint n:{0u,1u,2u,8u,113u,1024u}) for(uint scenario=0;scenario<4;++scenario) {
        float extent=scenario==0 ? 32.0f:8192.0f;
        for(uint k=0;k<3;++k) { worldModel.bounds[0][k]=-extent; worldModel.bounds[1][k]=extent; }
        pt.map_light_count=n;
        for(uint i=0;i<n;++i) {
            for(uint k=0;k<3;++k) {
                pt.map_positions[i][k]=(uniform()*2-1)*extent;
                pt.map_colors[i][k]=scenario==0 ? 0:scenario==1 ? 1:uniform()*8;
            }
            pt.map_positions[i][3]=scenario==3 && i==n/2 ? 1e8f:100.0f;
        }
        build_light_grid(); gridDimensions.w=n;
        if(!n) continue;
        for(uint cell=0;cell<pt.light_cells;++cell) {
            double sum=0;
            for(uint i=0;i<n;++i) {
                const float *entry=grid.entries[cell*n+i];
                assert(entry[0]>=0 && entry[0]<=1 && entry[1]>=0 && entry[1]<n);
                assert(entry[2]>=0.199999f/n);
                assert(!std::memcmp(&entry[3],&grid.entries[cell*n+uint(entry[1])][2],sizeof(float)));
                sum+=entry[2];
            }
            assert(std::abs(sum-1)<1e-5);
            for(uint sample=0;sample<1024;++sample) compare(cell,uniform());
            compare(cell,0); compare(cell,1); compare(cell,std::nextafter(1.0f,0.0f));
            // Float-rounded column/alias boundaries and adjacent inputs, across
            // all columns on corner/center cells of each native spatial grid.
            if(cell==0 || cell==pt.light_cells/2 || cell+1==pt.light_cells) {
                for(uint i=0;i<n;++i) for(float u:{float(i)/n,(i+grid.entries[cell*n+i][0])/n}) {
                    compare(cell,u); compare(cell,std::nextafter(u,0.0f)); compare(cell,std::nextafter(u,1.0f));
                }
            }
        }
    }
    printf("PASS: %llu exact light/PDF comparisons using native 0--1024-light grids; two table reads -> one; no distribution or buffer-size change\n",
        (unsigned long long)comparisons);
}
