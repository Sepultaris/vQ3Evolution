// Diagnostic-only, sparse workgroup replay. Never writes production images/history.
#ifndef PT_SOFTWARE_COUNTS
#extension GL_ARB_shader_clock : require
#endif
struct SoftwareProfileRecord { vec4 ticks[4]; uvec4 counts[8]; vec4 signal; };
layout(set=1,binding=0,std430) writeonly buffer SoftwareProfile {
    SoftwareProfileRecord softwareProfileRecords[];
};
bool profileEnabled=false;
uint profileCategory=0u,profileRecordIndex;
uvec2 profileLast;
vec4 profileTicks[4]=vec4[4](vec4(0),vec4(0),vec4(0),vec4(0));
uvec4 profileCounts[8]=uvec4[8](uvec4(0),uvec4(0),uvec4(0),uvec4(0),
    uvec4(0),uvec4(0),uvec4(0),uvec4(0));
vec4 profileSignal;
uint profileEnter(uint category) {
    uint previous=profileCategory;
    if(profileEnabled) {
#ifndef PT_SOFTWARE_COUNTS
        uvec2 now=clock2x32ARB();
        uvec2 elapsed=uvec2(now.x-profileLast.x,now.y-profileLast.y-uint(now.x<profileLast.x));
        profileTicks[previous>>2u][previous&3u]+=float(elapsed.x)+float(elapsed.y)*4294967296.0;
        profileLast=now;
#endif
        profileCategory=category;
    }
    return previous;
}
void profileBegin(uvec2 size,uint frame) {
    uvec2 groups=(size+7u)/8u,tile=gl_WorkGroupID.xy/8u;
    uvec2 selected=uvec2(frame&7u,(frame>>3u)&7u);
    // Exactly one visit to every workgroup per 64-frame cycle. Do not wrap
    // selection within edge tiles, which would oversample the image edges.
    profileEnabled=all(equal(gl_WorkGroupID.xy%8u,selected));
    profileRecordIndex=(tile.y*((groups.x+7u)/8u)+tile.x)*64u+gl_LocalInvocationIndex;
#ifndef PT_SOFTWARE_COUNTS
    if(profileEnabled) profileLast=clock2x32ARB();
#endif
}
void profileEnd() {
    if(profileEnabled) {
        profileEnter(0u);
        softwareProfileRecords[profileRecordIndex]=SoftwareProfileRecord(profileTicks,profileCounts,profileSignal);
    }
}
#define PT_CATEGORY(category) profileEnter(category)
#define PT_COUNT(category) if(profileEnabled) ++profileCounts[(category)>>2u][(category)&3u]
#define SW_COUNT(category) PT_COUNT(category)
// Counts: 16-18 nodes, 19-21 triangle tests, 22-24 mask rejections,
// 25-27 rejected boxes, 28-30 query starts (ordinary/visibility/weapon), 31 bounces.
#define SW_QUERY_KIND(q) ((q).mask==8u ? 2u : (((q).flags&4u)!=0u ? 1u:0u))
#define SW_QUERY_COUNT(base) SW_COUNT((base)+SW_QUERY_KIND(q))
