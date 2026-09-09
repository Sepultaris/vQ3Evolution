// The GLSL reservoir and budget functions are extracted without rewriting math.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>
using uint=unsigned;
using std::max; using std::ceil;
struct vec4 { float x=0,y=0,z=0,w=0; };
#include "rr_math.inc"
std::mt19937 rng(197);
float uniform() { return float(rng()>>8)*(1.0f/16777216.0f); }
float target(int light,int frame) { return 0.01f+((light+frame)%5)*0.27f; }
vec4 fresh(int frame) {
    vec4 r; float sum=0;
    for(int i=0;i<2;++i) { uint light=uint(uniform()*7); rrStream(r,sum,light,target(light,frame)*7,1,uniform()); }
    r.y=rrNormalize(sum,r.z,target(int(r.x),frame)); return r;
}
void merge(vec4 &r,float &sum,vec4 other,int frame) {
    float m=std::min(other.z,8.0f);
    rrStream(r,sum,uint(other.x),target(int(other.x),frame)*other.y*m,m,uniform());
}
int main() {
    double total=0,total2=0;
    const int trials=300000;
    // Changing target importance and changing visibility, with capped temporal
    // reuse + two independently generated spatial neighbors; exact light sum 28.
    for(int t=0;t<trials;++t) {
        vec4 old=fresh(0),r;
        for(int frame=1;frame<=9;++frame) {
            float sum=0; r={}; merge(r,sum,fresh(frame),frame); merge(r,sum,old,frame);
            r.y=rrNormalize(sum,r.z,target(int(r.x),frame)); old=r;
            merge(r,sum,fresh(frame+3),frame); merge(r,sum,fresh(frame+4),frame);
            r.y=rrNormalize(sum,r.z,target(int(r.x),frame));
        }
        double estimate=(r.x+1)*r.y; total+=estimate; total2+=estimate*estimate;
    }
    double mean=total/trials, error=std::sqrt((total2/trials-mean*mean)/trials);
    assert(std::abs(mean-28)<5*error && std::abs(mean-28)<0.25);
    assert(rrNormalize(0,0,0)==0);
    vec4 tracked{1,1.01f,16,0};
    assert(rrMovingHistorySafe(4,32,tracked));
    assert(!rrMovingHistorySafe(32,32,tracked));
    assert(!rrMovingHistorySafe(4,32,vec4{1,1.01f,11,0}));
    assert(!rrMovingHistorySafe(4,32,vec4{1,1.1f,16,0}));
    assert(rrAdaptiveSurfaceMatch(5,5,1,.4f,0,0,1));
    assert(!rrAdaptiveSurfaceMatch(5,6,1,.4f,0,0,1));
    assert(!rrAdaptiveSurfaceMatch(0,0,1,.4f,0,0,1));
    assert(!rrAdaptiveSurfaceMatch(5,5,.97f,.4f,0,0,1));
    assert(!rrAdaptiveSurfaceMatch(5,5,1,2.1f,0,0,1));
    assert(!rrAdaptiveSurfaceMatch(5,5,1,.4f,.16f,0,1));
    assert(!rrAdaptiveSurfaceMatch(5,5,1,.4f,0,.06f,1));
    for(float ceiling: {1.0f,2.0f,3.0f,4.0f,64.0f}) {
        vec4 stable{1,1.01f,16,0},noisy{1,4,16,0},newSurface{1,1,2,0};
        assert(rrSampleBudget(ceiling,stable,true)==ceil(ceiling*0.5f));
        assert(rrSampleBudget(ceiling,stable,false)==ceiling);
        assert(rrSampleBudget(ceiling,noisy,true)==ceiling);
        assert(rrSampleBudget(ceiling,newSurface,true)==ceiling);
    }
    printf("PASS: reused-light estimator mean %.5f (exact 28, standard error %.5f); capped history and adaptive budgets\n",mean,error);
}
