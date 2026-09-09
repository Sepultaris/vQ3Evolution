// Material stages, not separate geometry decals. Blend codes use the exact
// GLS_SRCBLEND/DSTBLEND nibble values in vk_shaders.h. No new GPU buffers.
bool materialLayerAccepted(vec4 color,int test) {
    return !((test==1 && color.a<=0) || (test==2 && color.a>=0.5) || (test==3 && color.a<0.5));
}
void materialBlendFactors(vec4 source,vec4 destination,uint blend,out vec4 sf,out vec4 df) {
    uint src=blend&15u,dst=(blend>>4)&15u;
    sf=vec4(1); df=vec4(0); // Zero state bits mean blending is disabled.
    if(src==1u) sf=vec4(0);
    if(src==3u) sf=destination;
    if(src==4u) sf=1-destination;
    if(src==5u) sf=vec4(source.a);
    if(src==6u) sf=vec4(1-source.a);
    if(src==7u) sf=vec4(destination.a);
    if(src==8u) sf=vec4(1-destination.a);
    if(src==9u) sf=vec4(vec3(min(source.a,1-destination.a)),1);
    if(dst==2u) df=vec4(1);
    if(dst==3u) df=source;
    if(dst==4u) df=1-source;
    if(dst==5u) df=vec4(source.a);
    if(dst==6u) df=vec4(1-source.a);
    if(dst==7u) df=vec4(destination.a);
    if(dst==8u) df=vec4(1-destination.a);
}
int materialBaseLayer(uint primitive) {
    return max(int(materials[triangleMaterials[primitive]&0xffffu].composition.y),0);
}
vec4 materialColor(uint primitive,vec2 bary,mat2 gradients,vec3 observer) {
    uint id=triangleMaterials[primitive]&0xffffu;
    vec4 composition=materials[id].composition;
    if(composition.y<0) return vec4(1); // Pure additive material has no backing.
    int base=int(composition.y);
    // A BSP mirror starts with unit reflected radiance, then applies the
    // authored tint/mask. In particular its transparent black first stage is
    // NOT a black wall. Actual reflected radiance comes from the GGX ray path.
    bool mirror=materials[id].maps.w==2;
    vec4 color=mirror ? vec4(1):vec4(0);
    // One sample site avoids duplicating the large UV/animation program in
    // every inlined caller. A one-base material still executes only one sample.
    int end=composition.w==0 ? base+1:int(composition.x);
    uint additiveMask=uint(composition.z);
    for(int layer=base;layer<end;++layer) {
        if((additiveMask&(1u<<uint(layer)))!=0u) continue;
        vec4 overlay=layerSample(primitive,bary,layer,gradients,observer);
        if(layer==base && !mirror) { color=overlay; continue; }
        vec4 generators=materials[id].layers[layer].generators;
        if(!materialLayerAccepted(overlay,int(generators.z))) continue;
        vec4 sf,df;
        materialBlendFactors(overlay,color,uint(generators.w),sf,df);
        // Legacy color layers are composed in texture space, then decoded
        // once by the physical material. Each layer keeps its own UV program.
        color=clamp(overlay*sf+color*df,0,1);
    }
    return color;
}
vec4 materialColor(uint primitive,vec2 bary,vec3 observer) {
    return materialColor(primitive,bary,textureBarycentrics(primitive),observer);
}
