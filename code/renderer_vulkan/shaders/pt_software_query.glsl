// Software candidate queries for the shared material/transport integrator.
// No acceleration structures, ray-query instructions or device addresses.
// Nodes have CPU-computed direction-ordered escape links: queries suspend at a candidate
// without retaining a per-ray traversal stack across material texture fetches.
layout(binding=0,std430) readonly buffer SoftwareNodes { vec4 swNodes[]; };
// Header: static node count, total node count, static triangle count, static ray mask.
layout(binding=50,std430) readonly buffer SoftwareLinks { uvec4 swTrees; uint swEscape[]; };
layout(binding=51,std430) readonly buffer SoftwareTriangles { uint swTriangles[]; };
struct SoftwareQuery {
    vec3 origin, direction;
    float minimum, maximum, candidateT, committedT;
    vec2 candidateBary, committedBary;
    uint node, cursor, end, mask, flags, candidatePrimitive, committedPrimitive;
    bool committed;
};
#define gl_RayFlagsNoneEXT 0u
#define gl_RayFlagsTerminateOnFirstHitEXT 4u
#define gl_RayQueryCandidateIntersectionTriangleEXT 1u
#define gl_RayQueryCommittedIntersectionNoneEXT 0u
#define rayQueryEXT SoftwareQuery
#define sceneAS 0u
#ifndef SW_QUERY_COUNT
#define SW_QUERY_COUNT(base)
#endif
void rayQueryInitializeEXT(out SoftwareQuery q,uint unused,uint flags,uint mask,
    vec3 origin,float minimum,vec3 direction,float maximum) {
    q.origin=origin; q.direction=direction; q.minimum=minimum; q.maximum=maximum;
    // A weapon-only query cannot hit the static BSP. Start at the dynamic
    // root when the whole static tree is excluded, before visiting any boxes.
    q.node=(mask&swTrees.w)!=0u ? 0u:swTrees.x;
    uint octant=uint(direction.x<0) | (uint(direction.y<0)<<1u) | (uint(direction.z<0)<<2u);
    q.cursor=0u; q.end=0u; q.mask=mask; q.flags=flags|(octant<<8u);
    SW_QUERY_COUNT(28u);
    q.candidateT=maximum; q.committedT=maximum;
    q.candidateBary=vec2(0); q.committedBary=vec2(0);
    q.candidatePrimitive=0u; q.committedPrimitive=0u; q.committed=false;
}
bool softwareBox(uint node,vec3 origin,vec3 direction,float minimum,float maximum) {
    vec3 lo=swNodes[2u*node].xyz, hi=swNodes[2u*node+1u].xyz;
    for(int axis=0;axis<3;++axis) {
        if(direction[axis]==0) {
            if(origin[axis]<lo[axis] || origin[axis]>hi[axis]) return false;
        } else {
            float a=(lo[axis]-origin[axis])/direction[axis];
            float b=(hi[axis]-origin[axis])/direction[axis];
            minimum=max(minimum,min(a,b)); maximum=min(maximum,max(a,b));
            if(minimum>maximum) return false;
        }
    }
    return true;
}
bool softwareTriangle(uint primitive,SoftwareQuery q,out float distance,out vec2 bary) {
    uint flags=triangleMaterials[primitive];
    uint mask=(flags&0x08000000u)!=0u ? 16u : ((flags&0x20000000u)!=0u ? 8u : 7u);
    if((mask&q.mask)==0u) { SW_QUERY_COUNT(22u); return false; }
    SW_QUERY_COUNT(19u);
    uvec3 t=triangle(primitive);
    vec3 a=position(t.x), e1=position(t.y)-a, e2=position(t.z)-a;
    vec3 p=cross(q.direction,e2);
    float det=dot(e1,p);
    if(abs(det)<1e-9) return false;
    vec3 s=q.origin-a, v=cross(s,e1);
    bary=vec2(dot(s,p),dot(q.direction,v))/det;
    if(bary.x<0 || bary.y<0 || bary.x+bary.y>1) return false;
    distance=dot(e2,v)/det;
    return distance>=q.minimum && distance<q.maximum;
}
bool softwareBoxReciprocal(uint node,vec3 origin,vec3 reciprocal,float minimum,float maximum) {
    vec3 a=(swNodes[2u*node].xyz-origin)*reciprocal;
    vec3 b=(swNodes[2u*node+1u].xyz-origin)*reciprocal;
    vec3 nearT=min(a,b),farT=max(a,b);
    float enter=max(nearT.x,max(nearT.y,nearT.z));
    float leave=min(farT.x,min(farT.y,farT.z));
    // Reciprocal multiplication can round differently from division. Loosen
    // only box bounds (never triangle tMin/tMax) to keep grazing hits visible.
    enter-=abs(enter)*0.0000006;
    leave+=abs(leave)*0.0000006;
    return max(minimum,enter)<=min(maximum,leave);
}
#ifdef PT_SOFTWARE_PROFILE
#define rayQueryProceedEXT softwareQueryProceed
#endif
bool rayQueryProceedEXT(inout SoftwareQuery q) {
    if(q.committed && (q.flags&gl_RayFlagsTerminateOnFirstHitEXT)!=0u) return false;
    // One reciprocal per axis per resume, not per visited box. No extra fields
    // in SoftwareQuery: these can be recomputed after material callbacks.
    // Parallel/near-parallel rays retain the original zero-safe slab test.
    bool reciprocalSafe=all(greaterThanEqual(abs(q.direction),vec3(1e-30)));
    vec3 reciprocal=vec3(0);
    if(reciprocalSafe) reciprocal=1.0/q.direction;
    for(;;) {
        while(q.cursor<q.end) {
            uint primitive=swTriangles[q.cursor++];
            if(softwareTriangle(primitive,q,q.candidateT,q.candidateBary)) {
                q.candidatePrimitive=primitive;
                return true; // The material callback decides whether to commit.
            }
        }
        if(q.node>=swTrees.y) return false;
        uint node=q.node;
        uint link=swEscape[8u*node+((q.flags>>8u)&7u)];
        SW_QUERY_COUNT(16u);
        if(!(reciprocalSafe ? softwareBoxReciprocal(node,q.origin,reciprocal,q.minimum,q.maximum):
            softwareBox(node,q.origin,q.direction,q.minimum,q.maximum))) {
            SW_QUERY_COUNT(25u);
            q.node=link&0x7fffffffu;
            continue;
        }
        uint left=floatBitsToUint(swNodes[2u*node].w);
        uint right=floatBitsToUint(swNodes[2u*node+1u].w);
        if((right&0x80000000u)!=0u) {
            q.cursor=left+(node>=swTrees.x ? swTrees.z:0u);
            q.end=q.cursor+(right&0x7fffffffu);
            q.node=link&0x7fffffffu;
        } else q.node=(link&0x80000000u)!=0u ? right:left;
    }
}
#ifdef PT_SOFTWARE_PROFILE
#undef rayQueryProceedEXT
bool rayQueryProceedEXT(inout SoftwareQuery q) {
    // Clock query resumes, not individual nodes or triangle tests. Material
    // callbacks execute after this interval and retain their own categories.
    uint previous=profileEnter(13u+SW_QUERY_KIND(q));
    bool result=softwareQueryProceed(q);
    profileEnter(previous);
    return result;
}
#endif
void rayQueryConfirmIntersectionEXT(inout SoftwareQuery q) {
    q.committed=true; q.committedPrimitive=q.candidatePrimitive;
    q.committedBary=q.candidateBary; q.committedT=q.candidateT;
    q.maximum=q.candidateT;
}
uint rayQueryGetIntersectionTypeEXT(SoftwareQuery q,bool committed) {
    return committed ? (q.committed ? 1u:0u):1u;
}
vec2 rayQueryGetIntersectionBarycentricsEXT(SoftwareQuery q,bool committed) {
    return committed ? q.committedBary:q.candidateBary;
}
float rayQueryGetIntersectionTEXT(SoftwareQuery q,bool committed) {
    return committed ? q.committedT:q.candidateT;
}
