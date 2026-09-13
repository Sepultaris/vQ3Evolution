/* Executes pt_material_layers.glsl itself with CPU vectors/mock samples. */
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
using byte = unsigned char;
#include "pt_world_vertex_type.inc"
static struct { bool active; pt_vertex_t world_attributes[3]; } pt;
#include "pt_world_vertex_upload.inc"
using uint = unsigned;
using std::min;
using std::max;
using std::abs;
using std::sqrt;
using std::pow;
static float max(float a,float b){return std::max(a,b);}
struct vec2 {};
struct mat2 { int footprint; mat2():footprint(37){} mat2(int n):footprint(n){} };
struct vec3 {
    float x,y,z;
    vec3(float f=0):x(f),y(f),z(f){}
    vec3(float x,float y,float z):x(x),y(y),z(z){}
    void operator*=(vec3 b){x*=b.x;y*=b.y;z*=b.z;}
    void operator+=(vec3 b){x+=b.x;y+=b.y;z+=b.z;}
    void operator-=(vec3 b){x-=b.x;y-=b.y;z-=b.z;}
};
static vec3 operator*(vec3 a,vec3 b){return {a.x*b.x,a.y*b.y,a.z*b.z};}
static vec3 operator+(vec3 a,vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
static vec3 operator-(vec3 a,vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
static vec3 operator/(vec3 a,float b){return {a.x/b,a.y/b,a.z/b};}
static vec3 operator/(vec3 a,vec3 b){return {a.x/b.x,a.y/b.y,a.z/b.z};}
static vec3 max(vec3 a,vec3 b){return {max(a.x,b.x),max(a.y,b.y),max(a.z,b.z)};}
static vec3 pow(vec3 a,vec3 b){return {float(pow(a.x,b.x)),float(pow(a.y,b.y)),float(pow(a.z,b.z))};}
struct vec4 {
    float x,y,z; union {float w,a;};
    vec4(float f=0):x(f),y(f),z(f),w(f){}
    vec4(float x,float y,float z,float w):x(x),y(y),z(z),w(w){}
    vec4(vec3 v,float a):x(v.x),y(v.y),z(v.z),w(a){}
    vec3 rgb()const{return {x,y,z};}
};
static vec4 operator+(vec4 a,vec4 b){return {a.x+b.x,a.y+b.y,a.z+b.z,a.w+b.w};}
static vec4 operator-(vec4 a,vec4 b){return {a.x-b.x,a.y-b.y,a.z-b.z,a.w-b.w};}
static vec4 operator*(vec4 a,vec4 b){return {a.x*b.x,a.y*b.y,a.z*b.z,a.w*b.w};}
static vec4 clamp(vec4 v,float lo,float hi){
    return {std::clamp(v.x,lo,hi),std::clamp(v.y,lo,hi),std::clamp(v.z,lo,hi),std::clamp(v.w,lo,hi)};
}
struct Record {vec4 composition,maps; struct {vec4 generators;} layers[9];};
static Record materials[1];
static uint triangleMaterials[1];
static vec4 samples[9];
static int reads[9];
static mat2 textureBarycentrics(uint){return {};}
static vec4 layerSample(uint primitive,vec2,int layer,mat2 gradients,vec3 observer){
    assert(primitive==0 && layer>=0 && layer<9 && (gradients.footprint==37 || gradients.footprint==0));
    assert(observer.x==7 && observer.y==8 && observer.z==9);
    ++reads[layer]; return samples[layer];
}
#include "pt_material_layers.inc"
static vec4 materialColor(uint p,vec2 b){return materialColor(p,b,vec3(7,8,9));}
struct Material {vec4 surface,emission,params,maps;};
static Material physical;
static Material materialProperties(uint){return physical;}
static vec4 layerSample(uint p,vec2 b,int l,vec3 observer){return layerSample(p,b,l,textureBarycentrics(p),observer);}
static vec4 triangleTexture(float,uint,vec2){return vec4(1);}
static vec3 linearColor(vec3 v){return {std::pow(v.x,2.2f),std::pow(v.y,2.2f),std::pow(v.z,2.2f)};}
static float clamp(float v,float a,float b){return std::clamp(v,a,b);}
static vec3 clamp(vec3 v,float a,float b){return {clamp(v.x,a,b),clamp(v.y,a,b),clamp(v.z,a,b)};}
static int randomCalls;
static float randomFloat(){++randomCalls;return .5f;}
#include "pt_glow_coverage.inc"
#include "pt_reflective_shell.inc"
// Weapon multipliers are independently tested by pt_emitter_geometry_check;
// these untagged material fixtures use unit emission.
static float weaponEmissionScale(uint,bool){return 1;}
static struct {vec4 sunExposure;} pc;
#include "pt_portal_coating.inc"
#include "pt_portal_tonemap.inc"
#include "pt_material_emission.inc"
static vec3 emissionAt(uint p,vec2 b,bool hit){return emissionAt(p,b,hit,vec3(7,8,9));}
static void expect(vec4 a,vec4 b){
    if (std::abs(a.x-b.x)>=1e-6 || std::abs(a.y-b.y)>=1e-6 ||
        std::abs(a.z-b.z)>=1e-6 || std::abs(a.w-b.w)>=1e-6)
        fprintf(stderr,"actual %.9g %.9g %.9g %.9g; expected %.9g %.9g %.9g %.9g; exposure %.9g\n",
            a.x,a.y,a.z,a.w,b.x,b.y,b.z,b.w,pc.sunExposure.w);
    assert(std::abs(a.x-b.x)<1e-6 && std::abs(a.y-b.y)<1e-6 &&
           std::abs(a.z-b.z)<1e-6 && std::abs(a.w-b.w)<1e-6);
}
static vec4 blend(vec4 s,vec4 d,uint mode){
    vec4 sf,df; materialBlendFactors(s,d,mode,sf,df); return clamp(s*sf+d*df,0,1);
}
static void reset(int count,int base,int mask,int overlays){
    materials[0]=Record{}; materials[0].composition={float(count),float(base),float(mask),float(overlays)};
    std::fill(reads,reads+9,0); physical=Material{};
}
int main(){
    // Q3DM0 aperture: alpha coating, rotating multiplicative stage, additive
    // wave, then distance-faded fog. The actual forward tone mapper must
    // recover the authored coating at every supported scene exposure.
    for(float exposure : {.01f,.1f,.5f,1.f,2.f,5.f,8.f,16.f})
    for(int fade=0;fade<=100;++fade) {
        pc.sunExposure.w=exposure;
        reset(4,0,4,3);
        materials[0].layers[0].generators.w=0x65;
        materials[0].layers[1].generators.w=0x13;
        materials[0].layers[2].generators.w=0x22;
        materials[0].layers[3].generators.w=0x65;
        samples[0]=vec4(.1f,.3f,.7f,.2f);
        samples[1]=vec4(.3f,.6f,.8f,1);
        samples[2]=vec4(.4f,.1f,.2f,1);
        samples[3]=vec4(.1f,.2f,.4f,fade/100.f);
        vec3 coating,transmission;
        portalCoating(0,{},vec3(7,8,9),coating,transmission);
        vec3 expected=samples[0].rgb()*.2f;
        expected=expected*samples[1].rgb()+samples[2].rgb();
        expected=samples[3].rgb()*(fade/100.f)+expected*(1-fade/100.f);
        expect(vec4(toneMap(coating),1),vec4(expected,1));
        vec3 expectedTransmission=linearColor(samples[1].rgb())*(.8f*(1-fade/100.f));
        expect(vec4(transmission,1),vec4(expectedTransmission,1));
        for(int i=0;i<4;++i) assert(reads[i]==1);
        if(fade==100) expect(vec4(transmission,1),vec4(0,0,0,1));
    }
    for(float exposure : {.01f,1.f,5.f,16.f}) {
        pc.sunExposure.w=exposure;
        for(int shade=0;shade<=255;++shade) {
            vec3 display(shade/255.f);
            expect(vec4(toneMap(portalArtworkRadiance(display)),1),vec4(display,1));
        }
    }
    puts("PASS: actual portal coating + forward tone mapper preserve authored brightness across exposure 0.01-16, all byte shades and alpha fades; HDR transmission unchanged");
    // Execute the real world uploader, then feed its interpolated alpha to
    // the real GLSL layer compositor. World alpha is not baked RGB lighting.
    const float terrainNormal[3]={0,0,1},terrainUV[2]={.25f,.75f};
    pt.active=true;
    for(int alpha=0;alpha<256;++alpha) {
        vk_pt_world_vertex(0,terrainNormal,terrainUV,byte(alpha));
        expect(vec4(pt.world_attributes[0].color[0],pt.world_attributes[0].color[1],
            pt.world_attributes[0].color[2],pt.world_attributes[0].color[3]),vec4(1,1,1,alpha/255.f));
        assert(pt.world_attributes[0].normal[2]==1 && pt.world_attributes[0].normal[3]==0);
        assert(pt.world_attributes[0].uv[0]==.25f && pt.world_attributes[0].uv[1]==.75f);
    }
    vk_pt_world_vertex(0,terrainNormal,terrainUV,0);
    vk_pt_world_vertex(1,terrainNormal,terrainUV,255);
    vk_pt_world_vertex(2,terrainNormal,terrainUV,0);
    for(int step=0;step<=100;++step) {
        float u=step/100.f,v=(1-u)*.3f;
        float alpha=pt.world_attributes[0].color[3]*(1-u-v)+
            pt.world_attributes[1].color[3]*u+pt.world_attributes[2].color[3]*v;
        reset(2,0,0,1); materials[0].layers[1].generators.w=0x65;
        samples[0]=vec4(.2f,.4f,.1f,alpha); samples[1]=vec4(.6f,.5f,.3f,alpha);
        expect(materialColor(0,{}),vec4(.2f+.4f*u,.4f+.1f*u,.1f+.2f*u,alpha));
        assert(passesAlpha(0,{},vec3(7,8,9))); // An opaque terrain blend is not a hole.
    }
    pt.active=false; vk_pt_world_vertex(0,terrainNormal,terrainUV,255);
    assert(pt.world_attributes[0].color[3]==0);
    puts("PASS: all 256 authored world alphas survive upload; terrain blends continuously without baked lighting or geometry holes");

    reset(2,0,0,1); materials[0].maps.w=2;
    samples[0]=vec4(0); samples[1]=vec4(.2f,.3f,.4f,1);
    materials[0].layers[0].generators.w=0x62; // ONE, ONE_MINUS_SRC_ALPHA
    materials[0].layers[1].generators.w=0x41; // ZERO, ONE_MINUS_SRC_COLOR
    expect(materialColor(0,{}),vec4(.8f,.7f,.6f,0));
    samples[0].a=1; expect(materialColor(0,{}),vec4(0));
    samples[0].a=.5f; expect(materialColor(0,{}),vec4(.4f,.35f,.3f,0));
    materials[0].maps.w=0; samples[0]=vec4(0);
    expect(materialColor(0,{}),vec4(0)); // Ordinary transparent black stays black.
    puts("PASS: actual mirror GLSL retains reflected radiance through black portal base, authored mask and alpha coating");
    const vec4 lava(1,.2f,0,1),stone(.3f,.3f,.3f,1),metal(.2f,.25f,.3f,1);
    reset(2,0,0,1); samples[0]=lava; samples[1]=stone;
    materials[0].layers[1].generators.w=0x65;
    expect(materialColor(0,{}),stone); // Opaque stone must hide the whole lava tile.
    samples[1].a=0; expect(materialColor(0,{}),lava); // Fire only through cracks.
    samples[1].a=.5f; expect(materialColor(0,{}),vec4(.65f,.25f,.15f,.75f));
    samples[1]=metal; expect(materialColor(0,{}),metal); // Same foreground rule for the wall grille.
    puts("PASS: actual GLSL hides lava under stone/metal; transparent and soft edges retain fire");

    reset(4,0,4,2); samples[0]=lava; samples[1]=stone; samples[2]=vec4(1); samples[3]=metal;
    materials[0].layers[1].generators.w=0x65; materials[0].layers[3].generators.w=0x13;
    expect(materialColor(0,{}),stone*metal);
    assert(reads[2]==0); // Additive light must not be folded into the reflectance.
    materials[0].layers[3].generators.w=0; expect(materialColor(0,{}),metal);
    puts("PASS: actual GLSL preserves alpha/filter/opaque order without baking glow into albedo");

    reset(2,0,0,1); samples[0]=lava; samples[1]=vec4(.3f,.3f,.3f,.25f);
    materials[0].layers[1].generators={0,0,3,0x65};
    expect(materialColor(0,{}),lava); // Failed overlay alpha test keeps the backing surface.
    samples[1].a=.75f; expect(materialColor(0,{}),blend(samples[1],lava,0x65));
    reset(2,1,1,0); samples[1]=stone; expect(materialColor(0,{}),stone);
    assert(materialBaseLayer(0)==1 && reads[0]==0 && reads[1]==1);
    reset(2,-1,3,0); expect(materialColor(0,{}),vec4(1)); assert(reads[0]==0 && reads[1]==0);
    puts("PASS: actual GLSL overlay alpha tests, shifted base and pure-additive fast path");

    vec4 s(.2f,.3f,.4f,.25f),d(.6f,.7f,.8f,.75f);
    expect(blend(s,d,0),s); expect(blend(s,d,0x12),s);
    expect(blend(s,d,0x13),s*d); expect(blend(s,d,0x31),s*d);
    expect(blend(s,d,0x22),clamp(s+d,0,1));
    expect(blend(s,d,0x25),clamp(s*vec4(s.a)+d,0,1));
    expect(blend(s,d,0x41),d*(vec4(1)-s));
    expect(blend(s,d,0x65),s*vec4(s.a)+d*vec4(1-s.a));
    expect(blend(s,d,0x17),s*vec4(d.a)); expect(blend(s,d,0x18),s*vec4(1-d.a));
    expect(blend(s,d,0x71),d*vec4(d.a)); expect(blend(s,d,0x81),d*vec4(1-d.a));
    expect(blend(s,d,0x19),s*vec4(vec3(.25f),1));
    puts("PASS: actual GLSL blend factors match opaque, additive, alpha and complementary filters");

    reset(2,1,1,0); physical.emission.y=1; physical.params.z=1;
    samples[0]=vec4(1,0,0,1); samples[1]=stone;
    materials[0].layers[0].generators.w=0x22;
    materials[0].layers[1].generators.w=0x65;
    expect(vec4(emissionAt(0,{},true),1),vec4(0,0,0,1));
    samples[1].a=0; expect(vec4(emissionAt(0,{},true),1),vec4(1,0,0,1));
    samples[1].a=.5f; expect(vec4(emissionAt(0,{},true),1),vec4(.5f,0,0,1));
    materials[0].layers[1].generators.w=0; expect(vec4(emissionAt(0,{},true),1),vec4(0,0,0,1));
    puts("PASS: actual GLSL foreground alpha/opaque layers mask earlier glow instead of leaking fire");

    reset(2,0,2,0); physical.emission.y=1; physical.params.z=1;
    samples[0]=stone; samples[1]=vec4(1,0,0,.5f);
    materials[0].layers[1].generators.w=0x25;
    expect(vec4(emissionAt(0,{},true),1),vec4(.5f,0,0,1));
    physical.emission.y=0; std::fill(reads,reads+9,0);
    expect(vec4(emissionAt(0,{},true),1),vec4(0,0,0,1));
    assert(reads[0]==0 && reads[1]==0);
    puts("PASS: actual GLSL alpha-weighted additive emission and zero-cost non-emitter rejection");

    reset(2,0,2,0); physical.emission.y=1; physical.params.z=1;
    physical.surface.w=3; physical.maps.w=3;
    materials[0].layers[0].generators.z=3;
    materials[0].layers[1].generators.w=0x22;
    samples[0]=vec4(.3f,.4f,.5f,0); samples[1]=vec4(1,.5f,0,1);
    assert(!passesAlpha(0,{},vec3(7,8,9))); // No solid shadow in a body hole.
    assert(surfaceHitMaterial(0,{},vec3(7,8,9)).params.x==2); // Glow + continue.
    expect(vec4(emissionAt(0,{},true),1),vec4(linearColor(samples[1].rgb()),1));
    expect(vec4(emissionAt(0,{},false),1),vec4(linearColor(samples[1].rgb()),1));
    materials[0].layers[1].generators.w=0x20022; // Authored depthFunc equal.
    expect(vec4(emissionAt(0,{},true),1),vec4(0,0,0,1));
    samples[0].a=1;
    assert(passesAlpha(0,{},vec3(7,8,9)));
    assert(surfaceHitMaterial(0,{},vec3(7,8,9)).params.x==0); // Lit solid + glow.
    expect(vec4(emissionAt(0,{},true),1),vec4(linearColor(samples[1].rgb()),1));
    samples[0].a=0; physical.maps.w=0;
    expect(vec4(emissionAt(0,{},true),1),vec4(0,0,0,1));
    assert(randomCalls==0); // Deterministic cutout, no extra coverage roulette.
    puts("PASS: actual GLSL hologram glows through body holes without occlusion; solid body, depth-equal masking and emitter sampling stay correct");

    reset(2,0,0,1); physical.emission.y=2; physical.emission.w=1;
    samples[0]=lava; samples[1]=stone;
    materials[0].layers[1].generators.w=0x65;
    // The compiler proxy is white, but a solid foreground must NOT emit.
    // Its authored RGB still supplies the physically lit reflectance.
    expect(materialColor(0,{}),stone);
    expect(vec4(emissionAt(0,{},true),1),vec4(0,0,0,1));
    expect(vec4(emissionAt(0,{},false),1),vec4(0,0,0,1));
    samples[1].a=0;
    expect(vec4(emissionAt(0,{},true),1),vec4(linearColor(lava.rgb())*2,1));
    samples[0]=metal;
    expect(vec4(emissionAt(0,{},true),1),vec4(linearColor(metal.rgb())*2,1));
    samples[1].a=.25f;
    expect(vec4(emissionAt(0,{},true),1),vec4(linearColor(metal.rgb())*1.5f,1));
    expect(vec4(emissionAt(0,{},false),1),vec4(linearColor(metal.rgb())*1.5f,1));
    materials[0].layers[1].generators.z=3; // Failed frame cutout keeps display.
    expect(vec4(emissionAt(0,{},true),1),vec4(linearColor(metal.rgb())*2,1));
    materials[0].layers[1].generators.z=0;
    materials[0].layers[1].generators.w=0;
    expect(vec4(emissionAt(0,{},true),1),vec4(0,0,0,1));
    puts("PASS: surface-light frames remain lit reflectance, block backing emission in camera/light samples, retain animation and soft/cutout masks");

    // Both spellings of a color filter must transmit the same base radiance.
    for(uint filter:{0x13u,0x31u,0x41u}) {
        reset(2,0,0,1); physical.emission.y=2;
        samples[0]=lava; samples[1]=metal;
        materials[0].layers[1].generators.w=filter;
        vec3 tint=filter==0x41u ? (vec4(1)-metal).rgb():metal.rgb();
        expect(vec4(emissionAt(0,{},true),1),vec4(linearColor(lava.rgb())*linearColor(tint)*2,1));
    }
    // A single-layer surface light after a removed lightmap is still emissive.
    reset(1,0,0,0); physical.emission.y=2; samples[0]=lava;
    materials[0].layers[0].generators.w=0x13;
    expect(vec4(emissionAt(0,{},true),1),vec4(linearColor(lava.rgb())*2,1));
    physical.surface.w=3; samples[0].a=0;
    expect(vec4(emissionAt(0,{},true),1),vec4(0,0,0,1));
    puts("PASS: surface-light RGB filters attenuate existing emission; single-layer lamps and alpha-tested lights remain correct");

    for(float cosine:{1.f,.8f,.5f,.1f,.001f}) for(float alpha:{0.f,.25f,1.f}){
        float f=dielectricFresnel(cosine,1,1.5f);
        vec3 r,t;
        reflectiveShellWeights(f,vec4(.8f,.3f,1,alpha),r,t);
        expect(vec4(r+t,1),vec4(1));
        assert(r.x>=0 && r.y>=0 && r.z>=0 && r.x<=1 && r.y<=1 && r.z<=1);
        if(alpha==0) expect(vec4(r,1),vec4(0,0,0,1));
        float probability=max(r.x,max(r.y,r.z));
        if(probability>0 && probability<1){
            vec3 sceneReflection(.2f,.7f,.4f),throughPickup(.6f,.1f,.9f);
            vec3 expected=r*sceneReflection+t*throughPickup;
            vec3 estimator=(r/probability)*sceneReflection*probability+
                (t/(1-probability))*throughPickup*(1-probability);
            expect(vec4(estimator,1),vec4(expected,1));
        }
    }
    vec3 normalR,normalT,edgeR,edgeT;
    reflectiveShellWeights(dielectricFresnel(1,1,1.5f),vec4(1),normalR,normalT);
    reflectiveShellWeights(dielectricFresnel(.01f,1,1.5f),vec4(1),edgeR,edgeT);
    assert(abs(normalR.x-2*.04f/1.04f)<1e-6 && edgeR.x>normalR.x);
    expect(vec4(normalR+normalT,1),vec4(1));
    puts("PASS: actual GLSL shell Fresnel/tint/alpha conserves energy; unbiased reflected-scene plus transmitted-pickup estimator");
    return 0;
}
