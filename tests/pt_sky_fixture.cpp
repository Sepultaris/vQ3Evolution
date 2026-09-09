/* Actual sky GLSL executed on the CPU with mock texture/stage sampling.
 * No window, game, Vulkan instance, GPU, or game assets are required. */
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
using uint=unsigned;
using std::abs; using std::sqrt; using std::min;
static float max(float a,float b){return std::max(a,b);}
struct vec2 {
    float x,y;
    vec2(float f=0):x(f),y(f){} vec2(float x,float y):x(x),y(y){}
};
struct vec3 {
    float x,y,z;
    vec3(float f=0):x(f),y(f),z(f){} vec3(float x,float y,float z):x(x),y(y),z(z){}
    vec2 xy()const{return {x,y};}
};
struct vec4 {
    float x,y,z; union{float w,a;};
    vec4(float f=0):x(f),y(f),z(f),w(f){}
    vec4(float x,float y,float z,float w):x(x),y(y),z(z),w(w){}
    vec4(vec3 v,float a):x(v.x),y(v.y),z(v.z),w(a){}
    float &operator[](int i){assert(i>=0 && i<4); return i==0?x:i==1?y:i==2?z:w;}
    vec3 xyz()const{return {x,y,z};} vec3 rgb()const{return xyz();}
};
struct mat2 {vec2 x,y; mat2(float f=0):x(f,0),y(0,f){} mat2(vec2 x,vec2 y):x(x),y(y){}};
static vec2 operator+(vec2 a,vec2 b){return {a.x+b.x,a.y+b.y};}
static vec2 operator-(vec2 a,vec2 b){return {a.x-b.x,a.y-b.y};}
static vec2 operator*(vec2 a,float b){return {a.x*b,a.y*b};}
static vec2 operator/(vec2 a,float b){return {a.x/b,a.y/b};}
static vec3 operator+(vec3 a,vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
static vec3 operator-(vec3 a,vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
static vec3 operator*(vec3 a,float b){return {a.x*b,a.y*b,a.z*b};}
static vec3 operator/(vec3 a,float b){return {a.x/b,a.y/b,a.z/b};}
static vec4 operator+(vec4 a,vec4 b){return {a.x+b.x,a.y+b.y,a.z+b.z,a.w+b.w};}
static vec4 operator-(vec4 a,vec4 b){return {a.x-b.x,a.y-b.y,a.z-b.z,a.w-b.w};}
static vec4 operator*(vec4 a,vec4 b){return {a.x*b.x,a.y*b.y,a.z*b.z,a.w*b.w};}
static float clamp(float v,float a,float b){return std::clamp(v,a,b);}
static vec2 clamp(vec2 v,vec2 a,vec2 b){return {clamp(v.x,a.x,b.x),clamp(v.y,a.y,b.y)};}
static vec4 clamp(vec4 v,float a,float b){return {clamp(v.x,a,b),clamp(v.y,a,b),clamp(v.z,a,b),clamp(v.w,a,b)};}
static vec3 abs(vec3 v){return {abs(v.x),abs(v.y),abs(v.z)};}
static float dot(vec3 a,vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static vec3 normalize(vec3 v){return v/std::sqrt(dot(v,v));}
static vec3 cross(vec3 a,vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static vec2 acos(vec2 v){return {std::acos(v.x),std::acos(v.y)};}
static vec3 mix(vec3 a,vec3 b,float t){return a*(1-t)+b*t;}
static vec3 linearColor(vec3 v){return {std::pow(v.x,2.2f),std::pow(v.y,2.2f),std::pow(v.z,2.2f)};}
struct LayerContext {vec3 local,localDx,localDy; vec4 vertexColor,entityColor; vec3 timeScroll; bool world,uniformTint;};
static struct {vec4 originNear,forwardFar;} pc;
static struct {vec4 emission,surface,skybox[2]; struct {vec4 generators;} layers[9];} materials[2];
static vec4 skyEnvironment;
static vec4 pixels[6],clouds[9];
static int faceRead,cloudReads;
static vec2 readUV,cloudUV;
static vec4 sampleTexture(float id,vec2 uv,mat2 gradients){
    assert(id>=0 && id<6 && std::isfinite(gradients.x.x) && std::isfinite(gradients.y.y));
    faceRead=int(id); readUV=uv; return pixels[int(id)];
}
static vec4 layerSampleUV(uint,int layer,vec2 uv,mat2 gradients,LayerContext context){
    assert(layer>=0 && layer<9 && context.world && context.timeScroll.x==0);
    assert(std::isfinite(gradients.x.x) && std::isfinite(gradients.y.y));
    ++cloudReads; cloudUV=uv; return clouds[layer];
}
#include "pt_sky_blending.inc"
#include "pt_sky.inc"
static void near(float a,float b,float tolerance=1e-5f){assert(std::abs(a-b)<tolerance);}
static void near(vec2 a,vec2 b){near(a.x,b.x);near(a.y,b.y);}
static void near(vec3 a,vec3 b){near(a.x,b.x);near(a.y,b.y);near(a.z,b.z);}
static void reset(){
    for(auto &m:materials){m={};for(int i=0;i<6;++i)m.skybox[i/4][i%4]=-1;}
    materials[0].emission.z=1; skyEnvironment=vec4(1,0,0,0);
    pc.originNear=vec4(0);pc.forwardFar=vec4(0,0,0,2048);
    faceRead=-1; cloudReads=0;
}
int main(){
    /* Independent inverse mapping from tr_sky.c's MakeSkyVec/sky_texorder. */
    const int axes[6][3]={{3,-1,2},{-3,1,2},{1,3,2},{-1,-3,2},{-2,-1,3},{2,-1,-3}};
    const int order[6]={0,2,1,3,4,5};
    for(int axis=0;axis<6;++axis) for(float s:{-.9f,-.2f,0.f,.8f}) for(float t:{-.8f,0.f,.3f,.9f}){
        float b[3]={s,t,1},v[3];
        for(int i=0;i<3;++i){int k=axes[axis][i];v[i]=(k<0?-1:1)*b[std::abs(k)-1];}
        vec3 d(v[0],v[1],v[2]); assert(skyFace(d)==order[axis]);
        near(skyFaceUV(d,order[axis]),vec2((s+1)*.5f,(1-t)*.5f));
        near(skyFaceUV(d*1000,order[axis]),skyFaceUV(d,order[axis]));
    }
    puts("PASS: actual GLSL sky face selection and UV orientation match all six raster faces (96 directions)");
    for(float height:{1.f,128.f,512.f,2048.f}) for(int z=-4;z<=4;++z){
        vec3 d=normalize(vec3(.3f,.5f,z*.25f));
        double dz=d.z,r=4096,h=height;
        double distance=-dz*r+std::sqrt(dz*dz*r*r+2*r*h+h*h);
        double vx=d.x*distance,vy=d.y*distance,vz=dz*distance+r;
        double length=std::sqrt(vx*vx+vy*vy+vz*vz);
        near(skyCloudUV(d,height),vec2(float(std::acos(vx/length)),float(std::acos(vy/length))));
    }
    near(skyCloudUV(vec3(0,0,1),512),vec2(float(std::acos(0.0))));
    puts("PASS: actual GLSL cloud projection agrees with the original radius-4096 shell at four heights");

    reset();
    vec3 directions[6]={{1,0,0},{0,1,0},{-1,0,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for(int i=0;i<6;++i){materials[0].skybox[i/4][i%4]=i;pixels[i]=vec4((i+1)/8.f,.25f,.5f,1);}
    for(int i=0;i<6;++i){near(skyRadiance(0,directions[i],.001f),linearColor(pixels[i].rgb()));assert(faceRead==i);near(readUV,vec2(.5f));}
    assert(cloudReads==0);
    vec3 color=skyRadiance(0,vec3(1,.3f,.4f),.001f);
    pc.originNear=vec4(1000,-300,720,0);
    near(skyRadiance(0,vec3(1,.3f,.4f),.001f),color);
    near(environment(vec3(1,.3f,.4f),.001f),color);
    puts("PASS: actual GLSL samples all six textures, survives camera translation and shares the miss/reflection environment");

    materials[0].skybox[1].z=512;materials[0].skybox[1].w=2;
    materials[0].layers[0].generators.w=0;materials[0].layers[1].generators.w=0x22;
    clouds[0]=vec4(.2f,.3f,.4f,1);clouds[1]=vec4(.1f,.2f,.3f,1);
    near(skyRadiance(0,vec3(0,0,1),.001f),linearColor(vec3(.3f,.5f,.7f)));
    near(cloudUV,skyCloudUV(vec3(0,0,1),512));assert(cloudReads==2);
    cloudReads=0; near(skyRadiance(0,vec3(0,0,-1),.001f),linearColor(pixels[5].rgb()));assert(cloudReads==0);
    materials[0].skybox[1].w=1;materials[0].layers[0].generators.w=0x65;clouds[0].a=.5f;
    near(skyRadiance(0,vec3(0,0,1),.001f),linearColor((clouds[0]*vec4(.5f)+pixels[4]*vec4(.5f)).rgb()));
    puts("PASS: actual GLSL composes opaque/additive/alpha clouds over the cube and leaves the bottom face intact");

    reset(); near(skyRadiance(0,vec3(0,0,1),.001f),vec3(0));assert(faceRead==-1 && cloudReads==0);
    materials[0].skybox[1].z=512;materials[0].skybox[1].w=1;clouds[0]=vec4(.4f,.5f,.6f,1);
    near(skyRadiance(0,vec3(0,0,1),.001f),linearColor(clouds[0].rgb()));assert(faceRead==-1);
    skyEnvironment.x=0;near(environment(vec3(0,0,1),.001f),vec3(.12f,.18f,.3f));
    puts("PASS: actual GLSL cloud-only/empty skies do not use a white fallback; gradient is restricted to maps with no sky");
}
