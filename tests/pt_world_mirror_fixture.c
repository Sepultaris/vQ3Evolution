/* Actual BSP mirror classifier and renderer parser; no GPU or game mutation. */
#include <assert.h>
#include <float.h>
#include <stdio.h>
#include "tr_globals.h"
#include "tr_shader.h"
#include "R_Parser.h"
trGlobals_t tr;
int Q_stricmp(const char *a,const char *b) { return stricmp(a,b); }
void Q_strncpyz(char *a,const char *b,int n) { snprintf(a,n,"%s",b); }
#include "pt_world_mirror.h"

int main(int argc,char **argv)
{
    world_t world={0}; shader_t shader={0}; msurface_t surface={0};
    srfSurfaceFace_t *face=calloc(1,sizeof(*face)+4*sizeof(face->points[0]));
    tr.world=&world; surface.shader=&shader; surface.data=(surfaceType_t *)face;
    shader.sort=SS_PORTAL; face->surfaceType=SF_FACE; face->numPoints=4;
    VectorSet(face->plane.normal,0,1,0); face->plane.dist=-1341;
    VectorSet(face->points[0],-1187,-1341,5); VectorSet(face->points[1],-1121,-1341,5);
    VectorSet(face->points[2],-1121,-1341,106); VectorSet(face->points[3],-1187,-1341,106);
    char mirror[]="{ \"classname\" \"misc_portal_surface\" \"origin\" \"-1152 -1316 56\" }";
    char remote[]="{ \"classname\" \"misc_portal_surface\" \"origin\" \"-1152 -1316 56\" \"target\" \"camera\" }";
    char shared[]="{ \"classname\" \"misc_portal_surface\" \"origin\" \"5000 -1316 56\" } "
        "{ \"target\" \"camera\" \"origin\" \"-1152 -1316 56\" \"classname\" \"misc_portal_surface\" }";
    world.entityString=mirror; world.entityParsePoint=mirror+3;
    assert(world_surface_is_mirror(&surface) && world.entityParsePoint==mirror+3);
    world.entityString=remote; assert(!world_surface_is_mirror(&surface));
    world.entityString=shared; assert(!world_surface_is_mirror(&surface));
    world.entityString=mirror; shader.sort=SS_OPAQUE; assert(!world_surface_is_mirror(&surface));
    shader.sort=SS_PORTAL; face->plane.dist=-1400; assert(!world_surface_is_mirror(&surface));
    face->plane.dist=-1341; face->surfaceType=SF_GRID; assert(!world_surface_is_mirror(&surface));
    face->surfaceType=SF_FACE; world.entityString=""; assert(!world_surface_is_mirror(&surface));
    world.entityString="{ \"classname\" \"misc_portal_surface\" }";
    assert(!world_surface_is_mirror(&surface));
    srfTriangles_t triangles={0}; drawVert_t verts[4]={0}; int indexes[3]={0,1,2};
    triangles.surfaceType=SF_TRIANGLES; triangles.numVerts=4; triangles.numIndexes=3;
    triangles.verts=verts; triangles.indexes=indexes;
    for(int i=0;i<4;++i) VectorCopy(face->points[i],verts[i].xyz);
    surface.data=(surfaceType_t *)&triangles; world.entityString=mirror;
    assert(world_surface_is_mirror(&surface));
    verts[3].xyz[1]+=2; assert(!world_surface_is_mirror(&surface));
    verts[3].xyz[1]-=2; indexes[0]=-1; assert(!world_surface_is_mirror(&surface));
    surface.data=(surfaceType_t *)face;
    if(argc==2) {
        FILE *f=fopen(argv[1],"rb"); assert(f); fseek(f,0,SEEK_END); long size=ftell(f); rewind(f);
        char *entities=calloc(1,size+1); assert(fread(entities,1,size,f)==(size_t)size); fclose(f);
        world.entityString=entities; assert(world_surface_is_mirror(&surface)); free(entities);
        puts("PASS: installed Q3DM0 entity data classifies its stock mirror, without shader-name special cases");
    }
    free(face);
    puts("PASS: native mirror/camera distinction, shared shader/plane ownership, missing entity, distance, unsupported surface, private parse cursor");
    return 0;
}
