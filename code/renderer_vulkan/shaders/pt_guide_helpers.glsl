// Reconstruction-only queries: no radiance estimator is modified here.
#include "pt_reflection_motion.glsl"
vec3 mirrorPoint(vec3 target,vec3 plane,vec3 normal) {
    return target-2*dot(target-plane,normal)*normal;
}

bool opticalResidual(vec3 point,vec3 camera,vec3 target,vec3 facing,vec3 u,vec3 v,
    vec2 optics,bool water,bool entering,bool reflection,float time,out vec2 residual) {
    vec3 start,outgoing;
    if(reflection) {
        if(!interfaceReflection(point,camera,facing,water,entering,time,pc.parameters.x,start,outgoing)) return false;
    } else if(!interfaceTransmission(point,camera,facing,optics,water,entering,time,pc.parameters.x,start,outgoing)) return false;
    float distance=dot(target-start,facing)/dot(outgoing,facing);
    if(distance<=0) return false;
    vec3 error=start+outgoing*distance-target;
    residual=vec2(dot(error,u),dot(error,v));
    return !any(isnan(residual)) && !any(isinf(residual));
}

// Invert the previous camera -> interface -> tracked target path on the old
// interface plane. A bounded Newton solve also accounts for spatially varying
// water normals at the PREVIOUS time, not just the wave under today's pixel.
// This traces no extra rays. Old guide/identity tests validate the finite mesh
// and target visibility at the resulting pixel before any history is reused.
bool previousOpticalPoint(vec3 seed,vec3 facing,vec3 target,vec2 optics,
    bool water,bool entering,bool reflection,out vec3 point) {
    point=seed;
    float targetSide=dot(target-seed,facing);
    if(dot(rp.previousOrigin.xyz-seed,facing)<=0.001 ||
        (reflection ? targetSide<=pc.parameters.x:targetSide>=-pc.parameters.x)) return false;
    vec3 u=normalize(cross(abs(facing.z)<0.999 ? vec3(0,0,1):vec3(0,1,0),facing));
    vec3 v=cross(facing,u);
    float pathDistance=reflection ? length(seed-rp.previousOrigin.xyz)+length(target-seed):length(target-rp.previousOrigin.xyz);
    float footprint=max(0.02,pathDistance*2/
        (float(imageSize(outputColor).y)*abs(rp.previousUp.w)));
    float epsilon=max(0.01,footprint*0.05),tolerance=max(0.005,footprint*0.1);
    for(int iteration=0;iteration<8;++iteration) {
        vec2 r,rx,ry;
        if(!opticalResidual(point,rp.previousOrigin.xyz,target,facing,u,v,optics,water,entering,reflection,rp.depthProjection.w,r)) return false;
        if(length(r)<=tolerance) return true;
        if(!opticalResidual(point+u*epsilon,rp.previousOrigin.xyz,target,facing,u,v,optics,water,entering,reflection,rp.depthProjection.w,rx) ||
           !opticalResidual(point+v*epsilon,rp.previousOrigin.xyz,target,facing,u,v,optics,water,entering,reflection,rp.depthProjection.w,ry)) return false;
        mat2 jacobian=mat2((rx-r)/epsilon,(ry-r)/epsilon);
        float determinantJ=determinant(jacobian);
        // Reject ill-conditioned correspondence rather than creating huge
        // motion. Old finite-interface and target tests reject wrong branches.
        if(abs(determinantJ)<0.0001) return false;
        vec2 step=inverse(jacobian)*r;
        if(any(isnan(step)) || any(isinf(step))) return false;
        step*=min(1,128/max(length(step),0.0001));
        point-=u*step.x+v*step.y;
    }
    vec2 residual;
    return opticalResidual(point,rp.previousOrigin.xyz,target,facing,u,v,optics,water,entering,reflection,rp.depthProjection.w,residual) && length(residual)<=tolerance;
}
bool previousTransmissionPoint(vec3 seed,vec3 facing,vec3 target,vec2 optics,
    bool water,bool entering,out vec3 point) {
    return previousOpticalPoint(seed,facing,target,optics,water,entering,false,point);
}
bool previousReflectionPoint(vec3 seed,vec3 facing,vec3 target,bool entering,out vec3 point) {
    return previousOpticalPoint(seed,facing,target,vec2(1,0),true,entering,true,point);
}
ReflectionGuide hitCorrespondence(Hit hit,vec3 world,vec3 direction,bool previous) {
    uvec3 t=triangle(hit.primitive);
    uint object=uint(vertices[t.x].normal.w);
    uint id=(triangleMaterials[hit.primitive]&0xffffu)|(object<<16);
    vec4 p=vec4(world,1);
    float area;
    vec3 normal=geometricNormal(hit.primitive,area);
    if(previous && object!=0u) {
        vec4 a=vertices[t.x].previous,b=vertices[t.y].previous,c=vertices[t.z].previous;
        p=vec4(a.xyz*(1-hit.bary.x-hit.bary.y)+b.xyz*hit.bary.x+c.xyz*hit.bary.y,min(a.w,min(b.w,c.w)));
        normal=cross(b.xyz-a.xyz,c.xyz-a.xyz);
        if(length(normal)<0.000001) p.w=0;
        normal/=max(length(normal),0.000001);
    }
    if(dot(normal,direction)>0) normal=-normal;
    if(object==0u && receiverHasDecal(hit.primitive,world)) {
        // Marked world targets cannot lend untracked decal history to reflected
        // or transmitted images, including the first frame after mark removal.
        id|=0xffff0000u;
        if(previous) p.w=0;
    }
    return ReflectionGuide(p,vec4(normal,uintBitsToFloat(id)));
}
#include "pt_transmission_path.glsl"
vec2 pointProbe(vec4 lamp,vec3 color,vec3 start,vec3 n,vec3 geometric,vec3 v,
    vec3 albedo,vec2 properties) {
    vec3 delta=lamp.xyz-start;
    float distance=length(delta);
    vec3 l=delta/max(distance,0.000001);
    if(lamp.w<=0 || distance<=0.04 || dot(n,l)<=0 || dot(geometric,l)<=0) return vec2(0);
    float pdf;
    vec3 brdf=evaluateBRDF(n,v,l,albedo,properties.x,properties.y,pdf);
    vec3 diffuse=(1-properties.y)*albedo/PI;
    vec3 incident=dot(n,l)*color*(lamp.w*lamp.w/max(distance*distance,1))*visibility(start,l,distance-0.02);
    return vec2(dot(diffuse*incident,vec3(0.2126,0.7152,0.0722)),
        dot(max(brdf-diffuse,vec3(0))*incident,vec3(0.2126,0.7152,0.0722)));
}
vec4 changedLighting(Hit hit,vec3 world,vec3 direction,vec3 n,vec3 geometric,vec2 properties) {
    vec4 result=vec4(0);
    if(lightSelection.w==0 || rp.jitterHistoryReset.w!=0) return result;
    vec3 albedo=baseColor(hit.primitive,hit.bary,pc.originNear.xyz),start=world+geometric*pc.parameters.x;
    for(uint i=0;i<max(lightCounts.y,uint(lightSelection.z));++i) {
        vec4 now=i<lightCounts.y ? pointPositions[i]:vec4(0);
        vec4 before=i<uint(lightSelection.z) ? previousPointPositions[i]:vec4(0);
        vec3 nowColor=i<lightCounts.y ? pointColors[i].rgb:vec3(0);
        vec3 oldColor=i<uint(lightSelection.z) ? previousPointColors[i].rgb:vec3(0);
        if(all(equal(now,before)) && all(equal(nowColor,oldColor))) continue;
        vec2 a=pointProbe(now,nowColor,start,n,geometric,-direction,albedo,properties);
        vec2 b=pointProbe(before,oldColor,start,n,geometric,-direction,albedo,properties);
        result+=vec4(a.x,b.x,a.y,b.y);
    }
    return result;
}
