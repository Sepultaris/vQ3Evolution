/* Real RB_StretchPic body; mock only draw batching and target transitions. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
enum { qfalse, qtrue };
typedef struct { void *shader; float x,y,w,h,s1,t1,s2,t2; } stretchPicCommand_t;
static struct { int projection2D, entity2D; void *currentEntity;
    unsigned char Color2D[4]; struct { struct { int time; } rd; float floatTime; } refdef; } backEnd;
static struct { void *shader; unsigned numIndexes, numVertexes;
    unsigned indexes[12]; unsigned char vertexColors[8][4];
    float xyz[8][3], texCoords[8][1][2]; } tess;
static int width,height,target_width,target_height,temporal_active,scene_pass,transitions;
static int milliseconds(void) { return 1000; }
static struct { int (*Milliseconds)(void); } ri = { milliseconds };
static void vk_temporal_begin_ui(void) {
    if(temporal_active && scene_pass) {
        scene_pass=0; target_width=width; target_height=height; ++transitions;
    }
}
static void RB_EndSurface(void) { tess.numIndexes=tess.numVertexes=0; }
static void RB_BeginSurface(void *shader,int fog) { (void)fog; tess.shader=shader; }
#define RB_CHECKOVERFLOW(v,i) assert(tess.numVertexes+(v)<=8 && tess.numIndexes+(i)<=12)
#include "pt_ui_target_code.inc"
int main(void) {
    const int extents[][4]={{1920,1080,1280,720},{1920,1080,1920,1080},
        {3440,1440,2293,960},{1280,1024,853,682}};
    int cases=0;
    for(unsigned size=0;size<sizeof(extents)/sizeof(extents[0]);++size)
    for(temporal_active=0;temporal_active<=1;++temporal_active) {
        width=extents[size][0]; height=extents[size][1]; backEnd.projection2D=qfalse;
        for(int frame=0;frame<6;++frame) {
            // Consecutive menu frames keep projection2D true. A world/model
            // view clears it; HUD models can already have begun the UI pass.
            if(frame==3 || frame==4) backEnd.projection2D=qfalse;
            scene_pass=temporal_active; transitions=0;
            target_width=temporal_active?extents[size][2]:width;
            target_height=temporal_active?extents[size][3]:height;
            if(frame==4) vk_temporal_begin_ui();
            stretchPicCommand_t cmd={ (void *)1, width*.5f,height*.5f,16,16,0,0,1,1 };
            RB_StretchPic(&cmd); RB_StretchPic(&cmd);
            if(target_width!=width || target_height!=height || scene_pass) {
                fprintf(stderr,"FAIL: frame %d retained scene target %dx%d for UI %dx%d\n",
                    frame,target_width,target_height,width,height); return 1;
            }
            assert(backEnd.projection2D && transitions==temporal_active);
            assert(tess.xyz[0][0]==width*.5f && tess.xyz[0][1]==height*.5f);
            assert(tess.numVertexes==8 && tess.numIndexes==12);
            RB_EndSurface(); ++cases;
        }
    }
    printf("PASS: %d native 2D-target cases: consecutive menus, gameplay/HUD-model transitions, DLAA/upscaling/disabled, wide and 4:3 displays\n",cases);
    return 0;
}
