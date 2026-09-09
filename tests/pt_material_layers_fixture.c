/* CPU-only regression fixture. The runner includes the actual production
 * material structs/functions; only renderer state and texture IDs are mocked. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include "tr_globals.h"
#include "tr_shader.h"
#include "vk_shaders.h"
#include "ref_import.h"
#include "pt_material_types.inc"

trGlobals_t tr;
refimport_t ri;
static pt_material_t output;
static struct {
    shader_t *material_shaders[MAX_SHADERS];
    unsigned material_count, animated_materials, effect_materials;
    qboolean materials_dirty;
    struct { void *mapped; } materials;
} pt;
static image_t images[16];
static shader_t shader;
static shaderStage_t stages[MAX_SHADER_STAGES];
static texModInfo_t mods[TR_MAX_TEXMODS];

static uint32_t texture_id(image_t *image) { return image ? image-images : 0; }
static void fail(int level, const char *format, ...) __attribute__((noreturn));
static void fail(int level, const char *format, ...) { (void)level; (void)format; abort(); }
#include "pt_material_code.inc"

static void reset(void) {
    memset(&pt,0,sizeof(pt)); memset(&shader,0,sizeof(shader));
    memset(stages,0,sizeof(stages)); memset(mods,0,sizeof(mods));
    pt.materials.mapped=&output; tr.whiteImage=images; ri.Error=fail;
    strcpy(shader.name,"textures/gothic_floor/fireblocks17floor3");
    shader.sort=SS_OPAQUE;
}
static shaderStage_t *append(unsigned blend,int image) {
    int i=shader.numUnfoggedPasses++;
    shaderStage_t *s=&stages[i];
    shader.stages[i]=s; s->stateBits=blend; s->active=qtrue;
    s->rgbGen=CGEN_IDENTITY; s->alphaGen=AGEN_IDENTITY;
    s->bundle[0].image[0]=&images[image]; s->bundle[0].tcGen=TCGEN_TEXTURE;
    memset(s->constantColor,255,4);
    return s;
}
static void check_image(int slot,int id) { assert(output.layers[slot].images[0][0]==id); }

int main(void) {
    const unsigned alpha=GLS_SRCBLEND_SRC_ALPHA|GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
    const unsigned add=GLS_SRCBLEND_ONE|GLS_DSTBLEND_ONE;
    const unsigned filter=GLS_SRCBLEND_DST_COLOR|GLS_DSTBLEND_ZERO;
    shaderStage_t *s;
    assert(sizeof(pt_material_t)==3312 && sizeof(pt_layer_t)==352);

    /* Team Arena terrain: two vertex-alpha textures plus cloud modulation. */
    reset();
    s=append(0,1); s->rgbGen=CGEN_VERTEX; s->alphaGen=AGEN_VERTEX;
    s->bundle[0].texMods=mods; s->bundle[0].numTexMods=1;
    mods[0].type=TMOD_SCALE; mods[0].scale[0]=mods[0].scale[1]=.125f;
    s=append(alpha,2); s->rgbGen=CGEN_VERTEX; s->alphaGen=AGEN_VERTEX;
    s->bundle[0].texMods=mods; s->bundle[0].numTexMods=1;
    append(filter,3);
    material_id(&shader);
    assert(output.composition[0]==3 && output.composition[3]==2 && output.params[0]==0);
    for(int i=0;i<2;++i) {
        assert(output.layers[i].meta[1]==AGEN_VERTEX && output.layers[i].vectors[0][3]==0);
        assert(output.layers[i].mods[0].a[1]==.125f && output.layers[i].mods[0].a[2]==.125f);
    }
    assert(output.layers[1].generators[3]==alpha && output.layers[2].generators[3]==filter);
    puts("PASS: terrain retains vertex-alpha generators, separate scaled layers, cloud filter and opaque coverage; no vertex-free shortcut");

    reset(); shader.sort=SS_PORTAL;
    append(GLS_SRCBLEND_ONE|GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA|GLS_DEPTHMASK_TRUE,1);
    append(GLS_SRCBLEND_ZERO|GLS_DSTBLEND_ONE_MINUS_SRC_COLOR,2);
    material_id(&shader);
    assert(output.params[0]==0 && output.surface[3]==0 && output.maps[3]==2);
    assert(output.surface[1]==.02f && output.surface[2]==1 && output.emission[1]==0);
    assert(output.composition[0]==2 && output.composition[3]==1);
    shader.rtMaterialDefined=qtrue; shader.rtRoughness=.1f; shader.rtMetallic=.9f;
    pt.material_shaders[0]=NULL; material_id(&shader);
    assert(output.surface[1]==.1f && output.surface[2]==.9f);
    puts("PASS: native BSP mirror is opaque to traversal, smooth reflective, non-emissive, preserves both coating stages and explicit PBR");

    /* Stock fire floor: animated lava, stationary alpha stone, then lightmap. */
    reset();
    s=append(0,1); s->bundle[0].texMods=mods; s->bundle[0].numTexMods=3;
    mods[0].type=TMOD_SCALE; mods[0].scale[0]=mods[0].scale[1]=.2f;
    mods[1].type=TMOD_SCROLL; mods[1].scroll[0]=.04f; mods[1].scroll[1]=.03f;
    mods[2].type=TMOD_TURBULENT; mods[2].wave.amplitude=.1f; mods[2].wave.frequency=.01f;
    append(alpha,2); append(filter,3)->bundle[0].isLightmap=qtrue;
    material_id(&shader);
    assert(output.composition[0]==2 && output.composition[1]==0);
    assert(output.composition[2]==0 && output.composition[3]==1);
    assert(output.params[0]==0 && output.params[2]==0 && output.params[3]==1);
    assert(output.layers[0].params[3]==3 && output.layers[1].params[3]==0);
    assert(output.layers[0].mods[1].a[1]==.04f);
    assert(output.layers[1].generators[3]==alpha);
    check_image(0,1); check_image(1,2);
    puts("PASS: native fire/stone conversion retains separate UV animation, alpha overlay, no lightmap");

    /* comp3/comp3b: scrolling display, discarded reflection/lightmap, frame.
     * Native conversion must retain the frame's alpha mask independently. */
    reset(); shader.rtSurfaceLight=1000; shader.rtLightImage=&images[4];
    s=append(0,1); s->bundle[0].numTexMods=1; s->bundle[0].texMods=mods;
    mods[0].type=TMOD_SCROLL; mods[0].scroll[0]=3; mods[0].scroll[1]=1;
    s=append(add,4); s->bundle[0].tcGen=TCGEN_ENVIRONMENT_MAPPED;
    append(GLS_SRCBLEND_DST_COLOR|GLS_DSTBLEND_ONE,3)->bundle[0].isLightmap=qtrue;
    append(alpha,2);
    append(GLS_SRCBLEND_DST_COLOR|GLS_DSTBLEND_ONE_MINUS_DST_ALPHA,3)->bundle[0].isLightmap=qtrue;
    material_id(&shader);
    assert(output.composition[0]==2 && output.composition[1]==0);
    assert(output.composition[2]==0 && output.composition[3]==1);
    assert(output.params[2]==0 && output.params[3]==1 && output.emission[1]==1);
    assert(output.layers[1].generators[3]==alpha);
    assert(output.layers[0].mods[0].a[1]==3 && output.layers[0].mods[0].a[2]==1);
    check_image(0,1); check_image(1,2);
    puts("PASS: native monitor conversion keeps animated display and stationary alpha frame separate from discarded reflection/lightmaps");

    /* Multiple overlays must keep their relative position around glow. */
    reset(); append(0,1); append(alpha,2); append(add,3); append(filter,4); append(0,5);
    material_id(&shader);
    assert(output.composition[0]==5 && output.composition[2]==4 && output.composition[3]==3);
    assert(output.params[2]==1);
    for(int i=0;i<5;++i) check_image(i,i+1);
    assert(output.layers[3].generators[3]==filter);
    assert(output.layers[4].generators[3]==0);
    puts("PASS: native ordering includes alpha/filter/opaque overlays and interleaved glow");

    reset(); append(add,1); append(alpha,2);
    material_id(&shader);
    assert(output.composition[1]==1 && output.composition[2]==1);
    assert(output.params[1]==1 && output.params[2]==1);
    check_image(0,1); check_image(1,2);
    reset(); append(add,1); append(add,2);
    material_id(&shader);
    assert(output.composition[1]==-1 && output.composition[2]==3);
    assert(output.params[0]==2 && output.params[1]==0);
    puts("PASS: leading glow does not displace the base; pure additive has no opaque backing");

    reset(); s=append(0,1); s->bundle[1]=s->bundle[0]; s->bundle[1].image[0]=&images[2];
    shader.multitextureEnv=GL_MODULATE; material_id(&shader);
    assert(output.layers[1].generators[3]==filter && output.composition[3]==1);
    pt.material_shaders[0]=NULL; shader.multitextureEnv=GL_ADD; material_id(&shader);
    assert(output.layers[1].generators[3]==add && output.composition[2]==2);
    puts("PASS: collapsed texture combines retain modulation/addition semantics");

    reset(); s=append(0,1); s->bundle[0].isLightmap=qtrue;
    append(filter,2); s=append(add,3); s->bundle[0].tcGen=TCGEN_ENVIRONMENT_MAPPED;
    material_id(&shader);
    assert(output.composition[0]==1 && output.composition[1]==0 && output.params[2]==0);
    check_image(0,2);
    reset(); material_id(&shader);
    assert(output.composition[0]==1 && output.composition[1]==0); check_image(0,0);
    puts("PASS: lightmap/backed-reflection exclusions and white fallback remain intact");

    /* Q3DM0 hologram: cutout body, sinusoidal move, scrolling environment glow.
     * The deformed glow must survive conversion, including in alpha holes,
     * without turning the body into an orb or baking glow into albedo. */
    reset(); shader.numDeforms=1;
    s=append(GLS_ATEST_GE_80|GLS_DEPTHMASK_TRUE,1); s->rgbGen=CGEN_LIGHTING_DIFFUSE;
    s=append(add,2); s->bundle[0].tcGen=TCGEN_ENVIRONMENT_MAPPED;
    s->bundle[0].texMods=mods; s->bundle[0].numTexMods=2;
    mods[0].type=TMOD_SCROLL; mods[0].scroll[0]=-6; mods[0].scroll[1]=-.2f;
    mods[1].type=TMOD_SCALE; mods[1].scale[0]=mods[1].scale[1]=1;
    material_id(&shader);
    assert(output.composition[0]==2 && output.composition[1]==0 && output.composition[2]==2);
    assert(output.params[0]==0 && output.params[1]==1 && output.params[2]==1 && output.params[3]==1);
    assert(output.surface[3]==3 && output.emission[1]==1 && output.maps[3]==3);
    assert(output.layers[1].params[2]==TCGEN_ENVIRONMENT_MAPPED && output.layers[1].params[3]==2);
    assert(output.layers[1].mods[0].a[1]==-6 && output.layers[1].mods[0].a[2]==-.2f);
    assert(output.layers[1].generators[3]==add && output.layers[1].vectors[0][3]==0);
    check_image(0,1); check_image(1,2);
    s->stateBits |= GLS_DEPTHFUNC_EQUAL; pt.material_shaders[0]=NULL; material_id(&shader);
    assert(output.maps[3]==-1 && output.emission[1]==1);
    assert(output.layers[1].generators[3]==(add|GLS_DEPTHFUNC_EQUAL));
    s->stateBits &= ~GLS_DEPTHFUNC_EQUAL;
    shader.numDeforms=0; pt.material_shaders[0]=NULL; material_id(&shader);
    assert(output.composition[0]==1 && output.params[2]==0 && output.emission[1]==0);
    shader.numDeforms=1; shader.rtDielectricDefined=qtrue; shader.rtIOR=1.5f;
    pt.material_shaders[0]=NULL; material_id(&shader);
    assert(output.params[0]==4 && output.composition[0]==1 && output.emission[1]==0);
    puts("PASS: hologram keeps scrolling glow, body coverage and independent/depth-equal stage semantics; reflective coats and dielectrics unchanged");

    reset(); s=append(0,1); s->bundle[1]=s->bundle[0]; s->bundle[1].image[0]=&images[2];
    shader.multitextureEnv=GL_MODULATE;
    for(int i=1;i<MAX_SHADER_STAGES;++i) append(alpha,i+2);
    material_id(&shader);
    assert(output.composition[0]==9 && output.composition[3]==8);
    for(int i=0;i<9;++i) check_image(i,i+1);
    puts("PASS: all nine layer slots fit within the allocated layer array");

    reset(); shader.isSky=qtrue; shader.sky.cloudHeight=512;
    for(int i=0;i<6;++i) shader.sky.outerbox[i]=&images[i+1];
    shader.rtLightImage=&images[9];
    material_id(&shader);
    for(int i=0;i<6;++i) assert(output.skybox[i/4][i%4]==i+1);
    assert(output.skybox[1][2]==512 && output.skybox[1][3]==0);
    assert(output.emission[0]==9 && output.emission[2]==1);
    assert(output.params[0]==0 && output.surface[3]==0);
    puts("PASS: native cube-only sky preserves all six face IDs; white fallback/lightimage are not cloud stages");

    reset(); shader.isSky=qtrue; shader.sky.cloudHeight=256;
    shader.sort=SS_ENVIRONMENT;
    s=append(alpha|GLS_ATEST_GE_80,1); append(add,2);
    s->bundle[0].texMods=mods; s->bundle[0].numTexMods=1;
    mods[0].type=TMOD_SCROLL; mods[0].scroll[0]=.15f;
    tr.defaultImage=&images[15]; shader.sky.outerbox[0]=tr.defaultImage;
    material_id(&shader);
    for(int i=0;i<6;++i) assert(output.skybox[i/4][i%4]==-1);
    assert(output.skybox[1][3]==2 && output.skybox[1][2]==256);
    assert(output.layers[0].mods[0].a[1]==.15f && output.params[3]==1);
    assert(output.layers[0].generators[3]==alpha && output.layers[0].generators[2]==3);
    assert(output.params[0]==0 && output.surface[3]==0);
    puts("PASS: native cloud-only sky retains animation/blending; absent faces and sky-boundary coverage remain correct");

    /* Stock health-cross pigment is uniform; physical metal owns reflection. */
    const char *crosses[]={"yellow","red","green","mega2"};
    for(int cross=0;cross<4;++cross) {
        reset(); snprintf(shader.name,sizeof(shader.name),"models/powerups/health/%s",crosses[cross]);
        s=append(0,1); s->bundle[0].tcGen=TCGEN_ENVIRONMENT_MAPPED;
        s->bundle[0].texMods=mods; s->bundle[0].numTexMods=1;
        mods[0].type=TMOD_ROTATE; mods[0].rotateSpeed=33;
        material_id(&shader);
        assert(output.params[0]==0 && output.surface[3]==0 && output.params[3]==0);
        assert(output.layers[0].vectors[0][3]==3 && output.surface[1]==.12f && output.surface[2]==1);
        assert(output.emission[1]==0 && output.params[2]==0);
        check_image(0,1);
        s->rgbGen=CGEN_WAVEFORM; pt.material_shaders[0]=NULL; material_id(&shader);
        assert(output.params[3]==1 && output.layers[0].vectors[0][3]==3);
    }
    reset(); strcpy(shader.name,"models/powerups/health/red");
    s=append(GLS_SRCBLEND_ONE|GLS_DSTBLEND_ZERO,1); s->bundle[0].tcGen=TCGEN_ENVIRONMENT_MAPPED;
    s=append(add,2); s->bundle[0].texMods=mods; s->bundle[0].numTexMods=1;
    mods[0].type=TMOD_SCROLL; mods[0].scroll[0]=mods[0].scroll[1]=9;
    material_id(&shader);
    assert(output.layers[0].vectors[0][3]==3 && output.layers[1].vectors[0][3]==2);
    assert(output.params[0]==0 && output.params[2]==1 && output.params[3]==1 && output.emission[1]==1);
    assert(output.composition[2]==2 && output.layers[1].mods[0].a[1]==9);
    assert(output.surface[2]==1); check_image(0,1); check_image(1,2);
    puts("PASS: native health crosses retain pigment and tint animation, trace polished metal, preserve the orange/mega electrical overlay");

    for(int exception=0;exception<7;++exception) {
        reset(); strcpy(shader.name,"models/powerups/health/yellow");
        s=append(0,1); s->bundle[0].tcGen=TCGEN_ENVIRONMENT_MAPPED;
        if(exception==0) strcpy(shader.name,"models/powerups/health_other/yellow");
        if(exception==1) shader.rtMaterialDefined=qtrue;
        if(exception==2) shader.rtBaseColorImage=&images[3];
        if(exception==3) append(alpha,2);
        if(exception==4) shader.rtSurfaceLight=100;
        if(exception==5) shader.numDeforms=1;
        if(exception==6) s->bundle[0].tcGen=TCGEN_TEXTURE;
        material_id(&shader); assert(output.layers[0].vectors[0][3]!=3);
    }
    reset(); strcpy(shader.name,"models/powerups/health/yellow_sphere");
    s=append(add,2); s->bundle[0].tcGen=TCGEN_ENVIRONMENT_MAPPED;
    material_id(&shader);
    assert(output.params[0]==4 && output.optical[1]<0 && output.layers[0].vectors[0][3]==0);
    puts("PASS: health-metal scope excludes unrelated artwork, explicit PBR/base maps, alpha composites, authored lights/deforms and orb shells");

    /* Unrelated environment artwork keeps its previous material rules. */
    reset(); s=append(0,1); s->bundle[0].tcGen=TCGEN_ENVIRONMENT_MAPPED;
    material_id(&shader);
    assert(output.composition[0]==1 && output.composition[1]==0);
    assert(output.params[0]==0 && output.params[1]==1 && output.params[2]==0 && output.params[3]==1);
    assert(output.layers[0].params[2]==TCGEN_ENVIRONMENT_MAPPED && output.layers[0].vectors[0][3]==0);
    assert(output.surface[1]==.3f && output.surface[2]==.85f && output.emission[1]==0);
    check_image(0,1);
    puts("PASS: unrelated environment artwork retains its color texture, reflective material and view-dependent history flag");

    /* Health shells are thin reflective sheets, with NO painted emission. */
    reset(); s=append(add,2); s->bundle[0].tcGen=TCGEN_ENVIRONMENT_MAPPED;
    s->bundle[0].texMods=mods; s->bundle[0].numTexMods=2;
    mods[0].type=TMOD_ROTATE; mods[0].rotateSpeed=33;
    mods[1].type=TMOD_SCROLL; mods[1].scroll[0]=mods[1].scroll[1]=1;
    s->rgbGen=CGEN_WAVEFORM; s->rgbWave.func=GF_TRIANGLE;
    s->rgbWave.base=-.3f; s->rgbWave.amplitude=1.3f; s->rgbWave.frequency=.3f;
    material_id(&shader);
    assert(output.composition[0]==1 && output.composition[1]==-1 && output.composition[2]==1);
    assert(output.params[0]==4 && output.params[1]==0 && output.params[2]==0 && output.emission[1]==0);
    assert(output.optical[0]==1.5f && output.optical[1]<0 && output.optical[2]==0);
    assert(output.params[3]==1); // The tint's color wave still animates.
    assert(output.layers[0].mods[0].a[1]==33 && output.layers[0].mods[1].a[1]==1);
    assert(output.layers[0].generators[0]==GF_TRIANGLE && output.layers[0].rgb_wave[0]==-.3f);
    check_image(0,2);
    puts("PASS: native environment shell uses non-emitting thin-sheet transport and retains animated tint data");
    s->rgbGen=CGEN_IDENTITY;
    pt.material_shaders[0]=NULL; material_id(&shader);
    assert(output.params[0]==4 && output.params[3]==0);
    assert(output.optical[1]<0 && output.emission[1]==0);
    puts("PASS: rotation/scroll of the old reflection image does not animate a stationary physical shell");
    shader.numDeforms=1; shader.deforms[0].deformation=DEFORM_WAVE;
    pt.material_shaders[0]=NULL; material_id(&shader);
    assert(output.params[0]==2 && output.params[2]==1 && output.emission[1]==1);
    assert(output.params[3]==1 && output.layers[0].mods[0].a[1]==33);
    puts("PASS: vertex-deformed quad/energy auras retain animated additive emission, not glass transport");
    shader.numDeforms=0;
    shader.rtSurfaceLight=3000;
    pt.material_shaders[0]=NULL; material_id(&shader);
    assert(output.params[0]==2 && output.params[2]==1 && output.emission[1]==3);
    puts("PASS: an explicitly authored environment-map light remains an emitter");
    reset(); s=append(add,1); s->bundle[0].tcGen=TCGEN_ENVIRONMENT_MAPPED;
    append(add,2); material_id(&shader);
    assert(output.params[0]==2 && output.params[2]==2 && output.emission[1]==1);
    puts("PASS: mixed texture/electric additive effects are not misclassified as a pure reflective shell");

    /* Red/mega crosses: reflective color base PLUS independently animated glow. */
    reset(); s=append(0,1); s->bundle[0].tcGen=TCGEN_ENVIRONMENT_MAPPED;
    append(add,2); material_id(&shader);
    assert(output.params[0]==0 && output.params[1]==1 && output.params[2]==1);
    assert(output.composition[1]==0 && output.composition[2]==2);
    check_image(0,1); check_image(1,2);
    /* Red/yellow armor: environment backing PLUS alpha-colored shell texture. */
    reset(); s=append(0,1); s->bundle[0].tcGen=TCGEN_ENVIRONMENT_MAPPED;
    append(alpha,2); material_id(&shader);
    assert(output.params[0]==0 && output.params[2]==0 && output.emission[1]==0);
    assert(output.composition[0]==2 && output.composition[1]==0 && output.composition[3]==1);
    assert(output.layers[1].generators[3]==alpha);
    check_image(0,1); check_image(1,2);
    puts("PASS: native health glow and armor alpha overlay keep their opaque environment-colored backing");

    /* Existing authored PBR and dielectric reflection behavior wins. */
    shader.rtMaterialDefined=qtrue; shader.rtRoughness=.7f; shader.rtMetallic=.1f;
    pt.material_shaders[0]=NULL; material_id(&shader);
    assert(output.surface[1]==.7f && output.surface[2]==.1f);
    reset(); shader.rtDielectricDefined=qtrue; shader.rtIOR=1.5f;
    s=append(0,1); s->bundle[0].tcGen=TCGEN_ENVIRONMENT_MAPPED; append(alpha,2);
    material_id(&shader);
    assert(output.params[0]==4 && output.composition[0]==1 && output.emission[1]==0);
    check_image(0,2);
    puts("PASS: native explicit PBR overrides and dielectric reflection exclusions remain intact");

    /* Classify every generator/modifier combination, not just one stock map.
     * Both optimized kinds must exclude position/normal/vertex-color inputs. */
    unsigned combinations = 0;
    for (int tc = TCGEN_BAD; tc <= TCGEN_VECTOR; ++tc)
    for (int rgb = CGEN_BAD; rgb <= CGEN_CONST; ++rgb)
    for (int alphaGen = AGEN_IDENTITY; alphaGen <= AGEN_CONST; ++alphaGen)
    for (int mod = TMOD_NONE; mod <= TMOD_ENTITY_TRANSLATE; ++mod) {
        pt_layer_t layer;
        reset(); s=append(0,1);
        s->rgbGen=rgb; s->alphaGen=alphaGen; s->bundle[0].tcGen=tc;
        s->bundle[0].numImageAnimations=2; s->bundle[0].image[1]=&images[2];
        s->bundle[0].numTexMods=1; s->bundle[0].texMods=mods; mods[0].type=mod;
        qboolean animated=layer_initialize(&layer,s,&s->bundle[0],&shader);
        qboolean expected=tc==TCGEN_TEXTURE && mod!=TMOD_TURBULENT &&
            (rgb==CGEN_IDENTITY || rgb==CGEN_IDENTITY_LIGHTING || rgb==CGEN_LIGHTING_DIFFUSE || rgb==CGEN_CONST || rgb==CGEN_WAVEFORM) &&
            (alphaGen==AGEN_IDENTITY || alphaGen==AGEN_SKIP || alphaGen==AGEN_CONST || alphaGen==AGEN_WAVEFORM);
        assert(layer.vectors[0][3]==(expected ? 2 : 0));
        assert(animated && layer.params[0]==2 && layer.images[0][1]==2);
        assert(layer.mods[0].a[0]==mod && layer.meta[0]==rgb && layer.meta[1]==alphaGen);
        ++combinations;
    }
    printf("PASS: native UV-only classification preserves program/animation across %u combinations\n",combinations);
    reset(); s=append(0,1);
    pt_layer_t layer;
    assert(!layer_initialize(&layer,s,&s->bundle[0],&shader) && layer.vectors[0][3]==1);
    s->bundle[0].numTexMods=TR_MAX_TEXMODS; s->bundle[0].texMods=mods;
    for(int i=0;i<TR_MAX_TEXMODS;++i) mods[i].type=TMOD_SCALE;
    assert(!layer_initialize(&layer,s,&s->bundle[0],&shader) && layer.vectors[0][3]==2);
    for(int i=0;i<TR_MAX_TEXMODS;++i) {
        mods[i].type=TMOD_TURBULENT;
        assert(layer_initialize(&layer,s,&s->bundle[0],&shader) && layer.vectors[0][3]==0);
        mods[i].type=TMOD_SCALE;
    }
    puts("PASS: immutable shortcut remains distinct; turbulence anywhere in the chain preserves full context");
    return 0;
}
