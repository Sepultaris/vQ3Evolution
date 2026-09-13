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
#include "pt_portal_transform.h"

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
    assert(world_surface_portal_kind(&surface)==2);
    world.entityString=shared; assert(!world_surface_is_mirror(&surface));
    world.entityString=mirror; shader.sort=SS_OPAQUE; assert(!world_surface_is_mirror(&surface));
    shader.sort=SS_PORTAL; face->plane.dist=-1400; assert(!world_surface_is_mirror(&surface));
    face->plane.dist=-1341; face->surfaceType=SF_GRID; assert(!world_surface_is_mirror(&surface));
    face->surfaceType=SF_FACE; world.entityString=""; assert(!world_surface_is_mirror(&surface));
    assert(world_surface_portal_kind(&surface)==0);
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
        world.entityString=entities; assert(world_surface_is_mirror(&surface));
        face->plane.dist=-1876.89f;
        for(int i=0;i<4;++i) face->points[i][1]=-1876.89f;
        shader.numDeforms=1;
        assert(world_surface_portal_kind(&surface)==2 && !world_surface_is_mirror(&surface));
        free(entities);
        puts("PASS: installed Q3DM0 entity data classifies its stock mirror, without shader-name special cases");
    }
    free(face);
    // R_MirrorPoint/Vector expressed independently in its two orthonormal bases.
    // Q3DM0 uses a 180-degree roll; also cover spinning/bobbing cameras.
    refEntity_t entity={0}; pt_portal_t transform;
    VectorSet(entity.origin,-1152,-1816,48);
    VectorSet(entity.oldorigin,188,-376,72);
    VectorSet(entity.axis[0],1,0,0); VectorSet(entity.axis[1],0,1,0); VectorSet(entity.axis[2],0,0,1);
    vec3_t normal={0,1,0}, basis[3], camera[3], point={-1130,-1876.89f,64};
    VectorCopy(normal,basis[0]); PerpendicularVector(basis[1],normal); CrossProduct(basis[0],basis[1],basis[2]);
    for(int mode=0;mode<3;++mode) for(int time=0;time<=10000;time+=137) {
        entity.skinNum=180; entity.oldframe=mode!=0; entity.frame=mode==1 ? 25:0;
        pt_portal_transform(&transform,normal,-1876.89f,&entity,time);
        float angle=mode==0 ? 180 : mode==1 ? time*0.001f*25 : 180+sinf(time*0.003f)*4;
        VectorScale(entity.axis[0],-1,camera[0]); VectorScale(entity.axis[1],-1,camera[1]);
        vec3_t temp; VectorCopy(camera[1],temp); RotatePointAroundVector(camera[1],camera[0],temp,angle);
        CrossProduct(camera[0],camera[1],camera[2]);
        vec3_t pivot={-1152,-1876.89f,48}, delta, expected;
        VectorSubtract(point,pivot,delta); VectorCopy(entity.oldorigin,expected);
        for(int axis=0;axis<3;++axis) VectorMA(expected,DotProduct(delta,basis[axis]),camera[axis],expected);
        for(int axis=0;axis<3;++axis) {
            assert(fabsf(DotProduct(transform.rows[axis],point)+transform.rows[axis][3]-expected[axis])<0.002f);
            assert(fabsf(DotProduct(transform.rows[axis],transform.rows[axis])-1)<0.0001f);
        }
        assert(fabsf(DotProduct(transform.clip,entity.oldorigin)-transform.clip[3])<0.0001f);
        assert((PT_PORTAL_MASK & 0xff800000u)==0 && ((63u<<16)&PT_PORTAL_MASK)==63u<<16);
    }
    puts("PASS: production camera transform matches independent point projection for fixed roll, spinning and bobbing cameras");
    puts("PASS: native mirror/camera distinction, shared shader/plane ownership, missing entity, distance, unsupported surface, private parse cursor");
    return 0;
}
