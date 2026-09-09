// Exact convex BSP volumes; immutable SSBO, independent of the surface BVH.
struct FogVolume { vec4 mins,maxs,albedo,emission; uvec4 planes; };
layout(binding=48,std430) readonly buffer FogVolumes {
    uvec4 fogControl;
    vec4 fogMins,fogMaxs;
    FogVolume fogVolumes[256];
    vec4 fogPlanes[];
};
bool fogClip(float signedOrigin,float slope,inout float entry,inout float exit) {
    if(abs(slope)<0.00000001) return signedOrigin<=0;
    float t=-signedOrigin/slope;
    if(slope<0) entry=max(entry,t); else exit=min(exit,t);
    return exit>entry;
}
bool fogBounds(vec3 mins,vec3 maxs,vec3 origin,vec3 direction,inout float entry,inout float exit) {
    for(int axis=0;axis<3;++axis) {
        if(abs(direction[axis])<0.00000001) {
            if(origin[axis]<mins[axis] || origin[axis]>maxs[axis]) return false;
        } else {
            float inverse=1/direction[axis];
            float a=(mins[axis]-origin[axis])*inverse,b=(maxs[axis]-origin[axis])*inverse;
            entry=max(entry,min(a,b)); exit=min(exit,max(a,b));
            if(exit<=entry) return false;
        }
    }
    return exit>entry;
}
bool fogInterval(uint volume,vec3 origin,vec3 direction,float minimum,float maximum,out vec2 interval) {
    float entry=minimum,exit=maximum;
    if(maximum<=minimum || fogVolumes[volume].mins.w<=0) return false;
    // Cheap AABB rejection precedes the authored non-axial clipping planes.
    if(!fogBounds(fogVolumes[volume].mins.xyz,fogVolumes[volume].maxs.xyz,origin,direction,entry,exit)) return false;
    uint first=fogVolumes[volume].planes.x,count=fogVolumes[volume].planes.y;
    for(uint i=0u;i<count;++i) {
        vec4 plane=fogPlanes[first+i];
        if(!fogClip(dot(plane.xyz,origin)-plane.w,dot(plane.xyz,direction),entry,exit)) return false;
    }
    interval=vec2(entry,exit);
    return exit>entry;
}
float fogTransmittance(vec3 origin,vec3 direction,float minimum,float maximum) {
    if(fogControl.x==0u || !fogBounds(fogMins.xyz,fogMaxs.xyz,origin,direction,minimum,maximum)) return 1;
    float tau=0;
    for(uint i=0u;i<fogControl.x;++i) {
        vec2 interval;
        if(fogInterval(i,origin,direction,minimum,maximum,interval))
            tau+=(interval.y-interval.x)*fogVolumes[i].mins.w;
    }
    return exp(-tau);
}
bool fogSample(vec3 origin,vec3 direction,float minimum,float maximum,out float distance,out uint volume) {
    distance=maximum; volume=0xffffffffu;
    if(fogControl.x==0u || !fogBounds(fogMins.xyz,fogMaxs.xyz,origin,direction,minimum,maximum)) return false;
    // Independent exponential processes superpose exactly, including overlaps.
    // The earliest event chooses its medium's scattering/emission coefficients.
    // No fixed ray march, interval array, lost small brush or repeated step bias.
    for(uint i=0u;i<fogControl.x;++i) {
        vec2 interval;
        if(!fogInterval(i,origin,direction,minimum,distance,interval)) continue;
        float t=interval.x-log(max(1-randomFloat(),0.00000001))/fogVolumes[i].mins.w;
        if(t<interval.y && t<distance) { distance=t; volume=i; }
    }
    return volume!=0xffffffffu;
}
bool fogScatter(uint volume,out vec3 weight) {
    // Analog absorption/scattering selection, compensated per color channel.
    // Emission is scored BEFORE this decision; it must not be roulette-killed.
    vec3 albedo=fogVolumes[volume].albedo.xyz;
    float probability=max(albedo.x,max(albedo.y,albedo.z));
    if(probability<=0 || (probability<1 && randomFloat()>=probability)) return false;
    weight=albedo/probability;
    PT_COUNT(5u);
    return true;
}
#ifdef PT_PROFILE_PASS
bool timedFogSample(vec3 origin,vec3 direction,float minimum,float maximum,out float distance,out uint volume) {
    uint previous=profileEnter(8u);
    bool event=fogSample(origin,direction,minimum,maximum,distance,volume);
    profileEnter(previous);
    return event;
}
float timedFogTransmittance(vec3 origin,vec3 direction,float minimum,float maximum) {
    uint previous=profileEnter(11u);
    float value=fogTransmittance(origin,direction,minimum,maximum);
    profileEnter(previous);
    return value;
}
#define fogSample timedFogSample
#define fogTransmittance timedFogTransmittance
#endif
