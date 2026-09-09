// Shared by native-to-RR packing and the direct RR tracer. Statistics describe
// fresh, unexposed noisy radiance; the reconstruction result never feeds back.
void rrStoreNoisy(ivec2 pixel,uint index,vec3 color) {
    if(any(isnan(color)) || any(isinf(color))) color=vec3(0);
    if(pc.sunRadiance.w>=2) {
        vec4 state=rrAdaptive[index];
        float luminance=dot(max(color,vec3(0)),vec3(0.2126,0.7152,0.0722));
        float age=min(state.z+1,32);
        float alpha=max(1/age,0.125);
        rrAdaptive[index]=vec4(mix(state.xy,vec2(luminance,luminance*luminance),alpha),age,state.w);
    }
    imageStore(rrNoisy,pixel,vec4(clamp(color,0,65504),1));
}
