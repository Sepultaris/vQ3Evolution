// Optional diagnostic variant only. These are exclusive, invocation-local
// clock shares, NOT additive GPU milliseconds or hardware-unit utilization.
#ifdef PT_PROFILE_PASS
#extension GL_ARB_shader_clock : require
struct ProfileRecord { vec4 ticks[3]; uvec4 counts[4]; };
layout(set=1,binding=0,std430) writeonly buffer ShaderProfile { ProfileRecord profileRecords[]; };
bool profileEnabled=false;
uint profileCategory=0u,profileRecordIndex;
uvec2 profileLast;
vec4 profileTicks[3]=vec4[3](vec4(0),vec4(0),vec4(0));
uvec4 profileCounts[4]=uvec4[4](uvec4(0),uvec4(0),uvec4(0),uvec4(0));
uint profileEnter(uint category) {
    uint previous=profileCategory;
    if(profileEnabled) {
        uvec2 now=clock2x32ARB();
        // Two-word subtraction handles low-word wrap without shaderInt64.
        uvec2 elapsed=uvec2(now.x-profileLast.x,now.y-profileLast.y-uint(now.x<profileLast.x));
        profileTicks[previous>>2u][previous&3u]+=float(elapsed.x)+float(elapsed.y)*4294967296.0;
        profileLast=now;
        profileCategory=category;
    }
    return previous;
}
void profileBegin(uvec2 size,uint frame) {
    uvec2 groups=(size+7u)/8u,tile=gl_WorkGroupID.xy/8u;
    uvec2 available=min(uvec2(8),groups-tile*8u);
    uvec2 selected=uvec2(frame&7u,(frame>>3u)&7u)%available;
    // Whole workgroups avoid introducing a single timed lane into each warp.
    // Rotate the selected group across frames; clamp partial edge tiles.
    profileEnabled=all(equal(gl_WorkGroupID.xy%8u,selected));
    profileRecordIndex=(tile.y*((groups.x+7u)/8u)+tile.x)*64u+gl_LocalInvocationIndex;
    if(profileEnabled) profileLast=clock2x32ARB();
}
void profileEnd() {
    if(profileEnabled) {
        profileEnter(0u);
        profileRecords[profileRecordIndex]=ProfileRecord(profileTicks,profileCounts);
    }
}
#define PT_CATEGORY(category) profileEnter(category)
#define PT_COUNT(category) if(profileEnabled) ++profileCounts[(category)>>2u][(category)&3u]
#else
#define PT_CATEGORY(category)
#define PT_COUNT(category)
#endif
