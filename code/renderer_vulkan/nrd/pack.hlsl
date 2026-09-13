// Engine-owned signal conversion for NRD RELAX. SDK helper implementation is
// supplied by the separately downloaded, pinned NRD SDK at build time.
// Must match the separately built SDK and adapter's encoding checks. The SDK
// helper does not choose these for us; undefined macros would square roughness.
#define NRD_NORMAL_ENCODING 3
#define NRD_ROUGHNESS_ENCODING 1
#include "NRD.hlsli"
[[vk::binding(0)]] StructuredBuffer<float4> diffuse;
[[vk::binding(1)]] StructuredBuffer<float4> specular;
[[vk::binding(2)]] StructuredBuffer<float4> positions;
[[vk::binding(3)]] StructuredBuffer<float4> albedos;
[[vk::binding(4)]] StructuredBuffer<float4> normals;
[[vk::binding(5)]] StructuredBuffer<float4> previousPositions;
[[vk::binding(6)]] StructuredBuffer<float4> transmission;
[[vk::binding(7)]] StructuredBuffer<float4> emission;
[[vk::binding(8)]] StructuredBuffer<float4> metadata;
[[vk::binding(9)]] StructuredBuffer<float4> lightChange;
[[vk::binding(10)]] StructuredBuffer<float4> nativeDiffuse;
[[vk::binding(11)]] StructuredBuffer<float4> nativeSpecular;
[[vk::binding(12)]] cbuffer Frame { float4 camera[8]; float4 previous[8]; uint4 dimensions; };
[[vk::binding(13),vk::image_format("rgba16f")]] RWTexture2D<float4> noisyD;
[[vk::binding(14),vk::image_format("rgba16f")]] RWTexture2D<float4> noisyS;
[[vk::binding(15),vk::image_format("rgba16f")]] RWTexture2D<float4> normalRoughness;
[[vk::binding(16),vk::image_format("r32f")]] RWTexture2D<float> viewZ;
[[vk::binding(17),vk::image_format("rgba16f")]] RWTexture2D<float4> motion;
[[vk::binding(18),vk::image_format("r32f")]] RWTexture2D<float> confidenceD;
[[vk::binding(19),vk::image_format("r32f")]] RWTexture2D<float> confidenceS;
[[vk::binding(20),vk::image_format("rgba16f")]] RWTexture2D<float4> resultD;
[[vk::binding(21),vk::image_format("rgba16f")]] RWTexture2D<float4> resultS;
[[vk::binding(22),vk::image_format("rgba8")]] RWTexture2D<float4> outputColor;

bool eligible(uint i) {
    // NRD is an opaque-surface denoiser. Do not flatten glass, fog or reactive
    // layers into the first surface's G-buffer or blur them into its neighbors.
    return positions[i].w>0 && metadata[i].w==0 && previousPositions[i].w>0;
}
void factors(uint i,out float3 d,out float3 s) {
    float3 n=normals[i].xyz;
    n=dot(n,n)>0.000001 ? normalize(n):float3(0,0,1);
    float3 v=camera[0].xyz-positions[i].xyz;
    v=dot(v,v)>0.000001 ? normalize(v):n;
    float metal=saturate(metadata[i].z);
    float3 base=albedos[i].rgb;
    NRD_MaterialFactors(n,v,base*(1-metal),lerp(float3(0.04,0.04,0.04),base,metal),
        saturate(normals[i].w),d,s);
}
[numthreads(8,8,1)]
void Prepare(uint3 pixel:SV_DispatchThreadID) {
    if(any(pixel.xy>=dimensions.xy)) return;
    uint i=pixel.y*dimensions.x+pixel.x;
    float3 df,sf; factors(i,df,sf);
    float3 n=normals[i].xyz;
    n=dot(n,n)>0.000001 ? normalize(n):float3(0,0,1);
    normalRoughness[pixel.xy]=NRD_FrontEnd_PackNormalAndRoughness(n,saturate(normals[i].w),0);
    viewZ[pixel.xy]=eligible(i) ? max(dot(positions[i].xyz-camera[0].xyz,camera[1].xyz),0.001):10000000;
    // World-space displacement: previous - current; camera motion is provided
    // by matrices, never baked into these vectors a second time.
    motion[pixel.xy]=float4(previousPositions[i].xyz-positions[i].xyz,0);
    noisyD[pixel.xy]=RELAX_FrontEnd_PackRadianceAndHitDist(diffuse[i].rgb/df,metadata[i].x,true);
    noisyS[pixel.xy]=RELAX_FrontEnd_PackRadianceAndHitDist(specular[i].rgb/sf,metadata[i].y,true);
    float2 energy=max(float2(dot(diffuse[i].rgb,float3(.2126,.7152,.0722)),
        dot(specular[i].rgb,float3(.2126,.7152,.0722))),0.01/max(camera[4].w,0.001));
    float2 reaction=smoothstep(.03,.3,abs(lightChange[i].xz-lightChange[i].yw)/energy);
    confidenceD[pixel.xy]=eligible(i) ? 1-reaction.x:0;
    confidenceS[pixel.xy]=eligible(i) ? 1-reaction.y:0;
}
[numthreads(8,8,1)]
void Compose(uint3 pixel:SV_DispatchThreadID) {
    if(any(pixel.xy>=dimensions.xy)) return;
    uint i=pixel.y*dimensions.x+pixel.x;
    if(positions[i].w<=0) return; // Reactive layers/sky retain raw integrator output.
    float3 df,sf; factors(i,df,sf);
    float3 d=nativeDiffuse[i].rgb, s=nativeSpecular[i].rgb;
    if(eligible(i)) {
        d=RELAX_BackEnd_UnpackRadiance(resultD[pixel.xy]).rgb*df;
        s=RELAX_BackEnd_UnpackRadiance(resultS[pixel.xy]).rgb*sf;
    }
    float3 radiance=d+s+transmission[i].rgb+emission[i].rgb;
    if(any(isnan(radiance)) || any(isinf(radiance))) return;
    float3 x=max(radiance*camera[4].w,0);
    float3 mapped=pow(saturate(x*(2.51*x+.03)/(x*(2.43*x+.59)+.14)),1.0/2.2);
    outputColor[pixel.xy]=float4(mapped,1);
}
