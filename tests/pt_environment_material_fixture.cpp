/* Compile the actual GLSL projection helpers on CPU; no GPU/game required. */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <algorithm>
using std::min;
struct vec2 {float x,y; vec2(float f):x(f),y(f){} vec2(float x,float y):x(x),y(y){}};
struct vec3 {float x,y,z; vec3(float f=0):x(f),y(f),z(f){} vec3(float x,float y,float z):x(x),y(y),z(z){}};
static vec3 operator+(vec3 a,vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
static vec3 operator-(vec3 a,vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
static vec3 operator*(vec3 a,float b){return {a.x*b,a.y*b,a.z*b};}
static vec3 operator*(float b,vec3 a){return a*b;}
static float dot(vec3 a,vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static vec3 cross(vec3 a,vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static float inversesqrt(float f){return 1/std::sqrt(f);}
static vec3 normalize(vec3 v){return v*inversesqrt(dot(v,v));}
struct mat3 {
    vec3 x,y,z;
    mat3(float f):x(f,0,0),y(0,f,0),z(0,0,f){}
    mat3(vec3 x,vec3 y,vec3 z):x(x),y(y),z(z){}
};
static vec3 operator*(mat3 a,vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static mat3 operator*(mat3 a,mat3 b){return {a*b.x,a*b.y,a*b.z};}
static mat3 transpose(mat3 a){return {{a.x.x,a.y.x,a.z.x},{a.x.y,a.y.y,a.z.y},{a.x.z,a.y.z,a.z.z}};}
#include "pt_environment_material.inc"
static void near(vec2 a,vec2 b){assert(std::abs(a.x-b.x)<2e-5 && std::abs(a.y-b.y)<2e-5);}
static void near(vec3 a,vec3 b){assert(std::abs(a.x-b.x)<2e-5 && std::abs(a.y-b.y)<2e-5 && std::abs(a.z-b.z)<2e-5);}
static vec3 rotate(vec3 a,float t){return {a.x*std::cos(t)-a.y*std::sin(t),a.x*std::sin(t)+a.y*std::cos(t),a.z};}
int main(){
    /* Independent scalar reference: RB_CalcEnvironmentTexCoords. */
    for(int i=0;i<100;++i){
        vec3 n=normalize(vec3(std::sin(i*.2f),std::cos(i*.3f),.1f+i*.02f));
        vec3 v=normalize(vec3(std::cos(i*.17f),std::sin(i*.23f),.5f));
        float d=n.x*v.x+n.y*v.y+n.z*v.z;
        vec2 expected(.5f+(n.y*2*d-v.y)*.5f,.5f-(n.z*2*d-v.z)*.5f);
        near(environmentMaterialUV(n,v,mat3(1)),expected);
        near(environmentMaterialUV(n*8,v*17,mat3(1)),expected);
    }
    puts("PASS: actual environment GLSL matches the legacy projection for 100 normal/view pairs and non-unit inputs");
    vec3 e1(2,.2f,0),e2(.1f,3,.4f),n=normalize(cross(e1,e2)),viewer=normalize(vec3(.3f,-.6f,.9f));
    for(float angle:{0.f,.7f,1.5f,3.14f,4.7f}) for(float scale:{.25f,1.f,5.f}){
        mat3 basis=environmentMaterialBasis(rotate(e1,angle)*scale,rotate(e2,angle)*scale,e1,e2);
        near(basis*rotate(n,angle),n);
        near(environmentMaterialUV(rotate(n,angle),rotate(viewer,angle),basis),environmentMaterialUV(n,viewer,mat3(1)));
    }
    puts("PASS: actual GLSL reconstructs model-local mapping through rotation/uniform scale without extra geometry data");
    near(environmentMaterialUV(vec3(0),viewer,mat3(1)),vec2(.5f));
    near(environmentMaterialUV(n,vec3(0),mat3(1)),vec2(.5f));
    mat3 degenerate=environmentMaterialBasis(vec3(0),e2,e1,e2);
    near(degenerate*viewer,viewer);
    puts("PASS: actual GLSL degenerate normals, coincident observer and collapsed triangle stay finite");
    vec3 point(20,-30,15),eye(150,50,60),shift(-1000,700,60);
    near(environmentMaterialUV(n,eye-point,mat3(1)),environmentMaterialUV(n,(eye+shift)-(point+shift),mat3(1)));
    vec2 primary=environmentMaterialUV(vec3(0,0,1),vec3(0,1,1),mat3(1));
    vec2 reflected=environmentMaterialUV(vec3(0,0,1),vec3(0,-1,1),mat3(1));
    assert(std::abs(primary.x-reflected.x)>.5f);
    assert(std::abs(primary.y-reflected.y)<2e-5);
    puts("PASS: actual GLSL is translation invariant and uses the reflected ray's observer instead of the player view");
    vec2 uv=environmentMaterialUV(n,eye-point,mat3(1));
    vec2 uvDx=environmentMaterialUV(n+vec3(.01f,0,.005f),eye-point-vec3(.1f,0,0),mat3(1));
    assert(std::isfinite(uvDx.x-uv.x) && std::isfinite(uvDx.y-uv.y));
    assert(std::abs(uvDx.x-uv.x)+std::abs(uvDx.y-uv.y)>.000001f);
    puts("PASS: actual GLSL varying normal/view footprint yields finite nonzero texture gradients");
}
