#ifndef PT_SOFTWARE_TRACE
#extension GL_EXT_ray_query : require
#endif
#extension GL_EXT_nonuniform_qualifier : require
#include "pt_shader_profile.glsl"
// Compile-time pipeline specialization: no per-pixel quality switch/buffer.
layout(constant_id=0) const bool ptMapLightCull=false;
layout(constant_id=4) const bool ptDlightReservoir=false;
#ifdef PT_SOFTWARE_NRD
layout(binding=52,std430) buffer SoftwareMetadata { vec4 softwareMetadata[]; };
// Measured first continuation distance; no extra visibility rays for denoising.
vec2 softwareHitDistance;
bool softwareDistancePending;
#endif

#ifdef PT_PARALLEL_SAMPLES
layout(local_size_x=4, local_size_y=4, local_size_z=4) in;
#elif defined(PT_WORKGROUP_ROWS)
layout(local_size_x=8, local_size_y=PT_WORKGROUP_ROWS) in;
#elif defined(PT_WORKGROUP_SPECIALIZATION)
layout(local_size_x=8, local_size_y=8, local_size_y_id=3) in;
#else
layout(local_size_x=8, local_size_y=8) in;
#endif
#ifndef PT_SOFTWARE_TRACE
layout(binding=0) uniform accelerationStructureEXT sceneAS;
#endif
layout(binding=3, rgba8) uniform writeonly image2D outputColor;
layout(binding=4, std430) readonly buffer Positions { float positions[]; };
layout(binding=5, std430) readonly buffer Indices { uint indices[]; };
struct Vertex { vec4 normal, uv, previous, meta, color, local; };
layout(binding=6, std430) readonly buffer Attributes { Vertex vertices[]; };
layout(binding=7, std430) readonly buffer TriangleMaterials { uint triangleMaterials[]; };
struct TexMod { vec4 a,b,wave; };
struct LayerRecord {
    vec4 images[2], params, color, generators, rgbWave, alphaWave, meta, vectors[2];
    TexMod mods[4];
};
struct MaterialRecord { vec4 surface, emission, composition, params, maps, optical, absorption; LayerRecord layers[9]; vec4 skybox[2]; };
layout(binding=8, std430) readonly buffer Materials { MaterialRecord materials[]; };
// Never copy the variable-indexed layer/modifier arrays into invocation-local
// storage. The record is 3312 bytes; doing so in every ray-candidate
// callback can spill large arrays and turn traversal into memory traffic.
struct Material { vec4 surface, emission, params, maps, optical, absorption; };
Material materialProperties(uint id) {
    return Material(materials[id].surface,materials[id].emission,materials[id].params,
        materials[id].maps,materials[id].optical,materials[id].absorption);
}
struct Layer { vec4 images[2], params, color, generators, rgbWave, alphaWave, meta, vectors[2]; };
Layer layerProperties(uint id,int layer) {
    return Layer(materials[id].layers[layer].images,materials[id].layers[layer].params,
        materials[id].layers[layer].color,materials[id].layers[layer].generators,
        materials[id].layers[layer].rgbWave,materials[id].layers[layer].alphaWave,
        materials[id].layers[layer].meta,materials[id].layers[layer].vectors);
}
layout(binding=9) uniform sampler2D textures[512];
struct Emitter { uint primitive; float cumulativePower; };
struct EmitterGeometry { vec4 a,b,c; };
struct Portal { vec4 rows[3]; vec4 clip; };
layout(binding=10, std430) readonly buffer Lights {
    uvec4 lightCounts;
    vec4 lightSelection;
    vec4 pointPositions[1056];
    vec4 pointColors[1056];
    vec4 pointCones[1056];
    Emitter emitters[8192];
    vec4 previousPointPositions[32], previousPointColors[32];
    vec4 decalMins,decalMaxs;
    vec4 skyEnvironment;
    uvec4 emitterSearchControl;
    uvec4 emitterSearchRanges[32]; // Two inclusive [low,high] pairs per vector.
    EmitterGeometry emitterGeometry[8192];
    vec4 rocketEmission;
    Portal portals[63];
};
layout(binding=11, std430) buffer Accumulation { vec4 accumulated[]; };
struct Guide { vec4 positionDepth; vec4 normalMaterial; vec4 albedoRoughness; };
layout(binding=12, std430) writeonly buffer GuidePositions { vec4 guidePositions[]; };
layout(binding=13, std430) writeonly buffer GuideNormals { vec4 guideNormals[]; };
layout(binding=14, std430) writeonly buffer GuideAlbedos { vec4 guideAlbedos[]; };
layout(binding=17, std430) readonly buffer Reprojection {
    vec4 previousOrigin, previousForward, previousRight, previousUp;
    vec4 jitterHistoryReset, options, depthProjection, cameraMedium;
} rp;
layout(binding=22, std430) writeonly buffer MotionPositions { vec4 motionPositions[]; };
layout(binding=23, std430) writeonly buffer MotionNormals { vec4 motionNormals[]; };
layout(binding=24, rg32f) uniform writeonly image2D outputMotion;
layout(binding=25, r32f) uniform writeonly image2D outputDepth;
layout(binding=26, std430) buffer Specular { vec4 specular[]; };
layout(binding=27, std430) buffer VisibleEmission { vec4 visibleEmission[]; };
layout(binding=32, std430) writeonly buffer ShadingGuides { vec4 shadingGuides[]; };
struct ReflectionGuide { vec4 position; vec4 normal; };
layout(binding=36, std430) writeonly buffer ReflectionGuides { ReflectionGuide reflectionGuides[]; };
layout(binding=38, std430) writeonly buffer ReflectionMotion { ReflectionGuide reflectionMotion[]; };
layout(binding=41, std430) writeonly buffer LightChange { vec4 lightChange[]; };
layout(binding=42, std430) buffer Transmission { vec4 transmission[]; };
#include "pt_transmission_guide.glsl"
layout(binding=45, std430) writeonly buffer TransmissionGuides { TransmissionGuide transmissionGuides[]; };
struct TransmissionMotion { vec4 position; vec4 normal; vec4 projection; };
layout(binding=47, std430) writeonly buffer TransmissionMotions { TransmissionMotion transmissionMotions[]; };
layout(push_constant) uniform Constants {
    vec4 originNear, forwardFar, rightProjection, upProjection;
    vec4 sunExposure, parameters, sunRadiance, sampling;
} pc;

const float PI=3.141592653589793;
uint rng;
#include "pt_sampling.glsl"
float randomFloat() {
    // Keep spatial blue-noise structure for pixel jitter and early light
    // samples. Later paths diverge by material/visibility; independent white
    // samples there avoid rank-buffer traffic without changing ray budgets.
    if(rp.options.w!=0 && sampleDimension<5u) return blueNoiseSample();
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return float(rng >> 8) * (1.0 / 16777216.0);
}
#include "pt_fog_volume.glsl"
vec3 position(uint index) {
    uint offset=index*3;
    return vec3(positions[offset], positions[offset+1], positions[offset+2]);
}
uvec3 triangle(uint primitive) {
    uint offset=primitive*3;
    return uvec3(indices[offset], indices[offset+1], indices[offset+2]);
}
uint portalIndex(uint primitive) { return (triangleMaterials[primitive]>>16)&63u; }
#include "pt_muzzle_flash.glsl"
vec2 triangleUV(uint primitive, vec2 bary) {
    uvec3 t=triangle(primitive);
    return vertices[t.x].uv.xy*(1-bary.x-bary.y) +
        vertices[t.y].uv.xy*bary.x + vertices[t.z].uv.xy*bary.y;
}
#include "pt_texture_footprint.glsl"
#include "pt_dielectric_geometry.glsl"
#ifdef PT_COMPACT_MEDIA
#include "pt_medium_records.glsl"
#define MEDIUM_IOR(index) mediumRecordIOR(mediumRecords[index])
#define MEDIUM_ABSORPTION(index) mediumRecordAbsorption(mediumRecords[index])
#else
#define MEDIUM_IOR(index) mediumIOR[index]
#define MEDIUM_ABSORPTION(index) mediumAbsorption[index]
#endif
vec3 linearColor(vec3 color) { return pow(max(color,vec3(0)),vec3(2.2)); }
// Contributions are partitioned by the first scattering lobe, not by a random
// label on the mixture sample. Their sum is exactly the original estimator.
#ifdef PT_COMPACT_TRANSPORT
#ifdef PT_RR_COMBINED
vec3 diffuseBRDF;
vec3 visibilityAbsorption;
#include "pt_rr_transport.glsl"
#else
vec3 radianceD,radianceS,radianceT,radianceE,diffuseBRDF;
vec3 visibilityAbsorption;
#include "pt_compact_transport.glsl"
#endif
#else
vec3 pathD, pathS, pathT, pathE, radianceD, radianceS, radianceT, radianceE, diffuseBRDF;
vec3 visibilityAbsorption;
void addIncident(vec3 light) {
    radianceD+=pathD*light; radianceS+=pathS*light; radianceT+=pathT*light; radianceE+=pathE*light;
}
void addDirect(vec3 brdf,vec3 light) {
    radianceD+=(pathD*brdf+pathE*diffuseBRDF)*light;
    radianceS+=(pathS*brdf+pathE*max(brdf-diffuseBRDF,vec3(0)))*light;
    radianceT+=pathT*brdf*light;
}
void attenuate(vec3 value) { pathD*=value; pathS*=value; pathT*=value; pathE*=value; }
#endif
#include "pt_ambient.glsl"
#include "pt_wave.glsl"
// Explicit stage inputs allow sky clouds and surface materials to use the same
// animation/UV/color program, without fabricating a triangle for the sky.
struct LayerContext {
    vec3 local,localDx,localDy;
    vec4 vertexColor,entityColor;
    vec3 timeScroll;
    bool world,uniformTint;
};
vec4 layerSampleUV(uint id,int layerIndex,vec2 uv,mat2 gradients,LayerContext context) {
    if(materials[id].layers[layerIndex].vectors[0].w==1)
        return sampleTexture(materials[id].layers[layerIndex].images[0].x,uv,gradients)*
            materials[id].layers[layerIndex].vectors[1];
#ifdef PT_MATERIAL_CACHE
    // Cache only native-classified UV/time-only stages with an unused modifier
    // slot. Entity shader time/scroll and shell pigment sampling stay exact.
    if(materials[id].layers[layerIndex].vectors[0].w==2 && !context.uniformTint &&
        all(equal(context.timeScroll,vec3(0))) && materials[id].layers[layerIndex].params.w<4 &&
        materials[id].layers[layerIndex].mods[3].b.w==1) {
        TexMod cached=materials[id].layers[layerIndex].mods[3];
        // Preserve authored transform order; share only frame-constant values.
        for(int i=0;i<int(materials[id].layers[layerIndex].params.w);++i) {
            TexMod m=materials[id].layers[layerIndex].mods[i];
            int type=int(m.a.x);
            vec2 value=i==0 ? cached.a.xy:(i==1 ? cached.a.zw:cached.b.xy);
            if(type==1) {
                uv=vec2(dot(vec3(uv,1),m.a.yzw),dot(vec3(uv,1),m.b.xyz));
                gradients=mat2(m.a.y,m.b.x,m.a.z,m.b.y)*gradients;
            }
            if(type==3) uv+=value;
            if(type==4) { uv*=m.a.yz; gradients[0]*=m.a.yz; gradients[1]*=m.a.yz; }
            if(type==5) {
                float stretch=value.x;
                if(abs(stretch)>0.00001) { uv=(uv-0.5)/stretch+0.5; gradients/=stretch; }
            }
            if(type==6) {
                float c=value.x,s=value.y;
                uv=mat2(c,s,-s,c)*(uv-0.5)+0.5;
                gradients=mat2(c,s,-s,c)*gradients;
            }
        }
        return sampleTexture(cached.b.z,uv,gradients)*cached.wave;
    }
#endif
    Layer layer=layerProperties(id,layerIndex);
    vec3 local=context.local,localDx=context.localDx,localDy=context.localDy;
    vec4 vertexColor=context.vertexColor,entityColor=context.entityColor;
    float time=rp.depthProjection.z-context.timeScroll.x-layer.meta.w;
    if(layer.meta.z>0) time=min(time,layer.meta.z);
    if(layer.params.z==1) { uv=vec2(0); gradients=mat2(0); }
    if(layer.params.z==6) {
        uv=vec2(dot(local,layer.vectors[0].xyz),dot(local,layer.vectors[1].xyz));
        gradients=mat2(vec2(dot(localDx,layer.vectors[0].xyz),dot(localDx,layer.vectors[1].xyz)),
            vec2(dot(localDy,layer.vectors[0].xyz),dot(localDy,layer.vectors[1].xyz)));
    }
    for(int i=0;!context.uniformTint && i<int(layer.params.w);++i) {
        TexMod m=materials[id].layers[layerIndex].mods[i];
        int type=int(m.a.x);
        if(type==1) {
            uv=vec2(dot(vec3(uv,1),m.a.yzw),dot(vec3(uv,1),m.b.xyz));
            gradients=mat2(m.a.y,m.b.x,m.a.z,m.b.y)*gradients;
        }
        if(type==2) {
            vec2 phase=(vec2(local.x+local.z,local.y)/1024+m.wave.z+time*m.wave.w)*2*PI;
            uv+=sin(phase)*m.wave.y;
            vec2 slope=cos(phase)*(m.wave.y*2*PI/1024);
            gradients[0]+=slope*vec2(localDx.x+localDx.z,localDx.y);
            gradients[1]+=slope*vec2(localDy.x+localDy.z,localDy.y);
        }
        if(type==3) uv+=fract(m.a.yz*time);
        if(type==4) { uv*=m.a.yz; gradients[0]*=m.a.yz; gradients[1]*=m.a.yz; }
        if(type==5) {
            float stretch=waveValue(m.wave,int(m.b.w),time);
            if(abs(stretch)>0.00001) { uv=(uv-0.5)/stretch+0.5; gradients/=stretch; }
        }
        if(type==6) {
            float angle=-m.a.y*time*PI/180, c=cos(angle), s=sin(angle);
            uv=mat2(c,s,-s,c)*(uv-0.5)+0.5;
            gradients=mat2(c,s,-s,c)*gradients;
        }
        if(type==7) uv+=fract(context.timeScroll.yz*time);
    }
    int frame=int(max(floor(time*layer.params.y),0))%max(int(layer.params.x),1);
    float textureIndex=layer.images[frame/4][frame%4];
    vec4 sampled;
    if(context.uniformTint) {
        // The coarsest mip supplies a spatially uniform pigment, never an image
        // of a fake room/highlight. No observer/normal/UV drives this lookup.
        sampled=textureLod(textures[nonuniformEXT(uint(textureIndex))],vec2(0.5),
            float(textureQueryLevels(textures[nonuniformEXT(uint(textureIndex))])-1));
        float peak=max(sampled.r,max(sampled.g,sampled.b));
        sampled.rgb=peak>0 ? sampled.rgb/peak:vec3(0);
    } else sampled=sampleTexture(textureIndex,uv,gradients);
    vec3 color=vec3(1);
    int rgb=int(layer.meta.x), alpha=int(layer.meta.y);
    if(rgb==3) color=entityColor.rgb;
    if(rgb==4) color=1-entityColor.rgb;
    // Baked world vertex lighting is deliberately not an albedo multiplier.
    if(!context.world && (rgb==5 || rgb==6)) color=vertexColor.rgb;
    if(!context.world && rgb==7) color=1-vertexColor.rgb;
    if(rgb==8) color=vec3(clamp(waveValue(layer.rgbWave,int(layer.generators.x),time),0,1));
    if(rgb==11) color=layer.color.rgb;
    float opacity=1;
    if(alpha==2) opacity=entityColor.a;
    if(alpha==3) opacity=1-entityColor.a;
    if(alpha==4) opacity=vertexColor.a;
    if(alpha==5) opacity=1-vertexColor.a;
    if(alpha==7) opacity=clamp(waveValue(layer.alphaWave,int(layer.generators.y),time),0,1);
    if(alpha==9) opacity=layer.color.a;
    return sampled*vec4(color,opacity);
}
#ifdef PT_PROFILE_PASS
vec4 timedLayerSampleUV(uint id,int layerIndex,vec2 uv,mat2 gradients,LayerContext context) {
    uint previous=profileEnter(2u);
    vec4 value=layerSampleUV(id,layerIndex,uv,gradients,context);
    profileEnter(previous);
    return value;
}
#define layerSampleUV timedLayerSampleUV
#endif
#include "pt_environment_material.glsl"
vec4 layerSample(uint primitive,vec2 bary,int layerIndex,mat2 baryGradients,vec3 observer,bool uniformTint) {
    uint id=triangleMaterials[primitive]&0xffffu;
    vec2 uv=triangleUV(primitive,bary);
    mat2 gradients=textureGradients(primitive,baryGradients);
    float program=materials[id].layers[layerIndex].vectors[0].w;
    // Opaque health metal uses the same uniform pigment sampling as its orb,
    // with the actual reflected image supplied by its opaque metallic BRDF.
    uniformTint=uniformTint || program==3;
    if(program==1)
        return sampleTexture(materials[id].layers[layerIndex].images[0].x,uv,gradients)*
            materials[id].layers[layerIndex].vectors[1];
    uvec3 t=triangle(primitive);
    vec3 weights=vec3(1-bary.x-bary.y,bary);
    LayerContext context;
    context.uniformTint=uniformTint;
    context.timeScroll=vertices[t.x].meta.xyz;
    if(program==2) {
        // Native classification proves these inputs are unused: texture UVs,
        // affine/time modifiers, fixed/wave colors. Preserve entity time and
        // scroll above; leave the shared animation/gradient program unchanged.
        context.local=context.localDx=context.localDy=vec3(0);
        context.vertexColor=context.entityColor=vec4(1);
        context.world=true;
    } else {
    Vertex v=vertices[t.x];
    context.world=v.normal.w==0;
    context.local=context.world ? position(t.x)*weights.x+position(t.y)*weights.y+position(t.z)*weights.z :
        vertices[t.x].local.xyz*weights.x+vertices[t.y].local.xyz*weights.y+vertices[t.z].local.xyz*weights.z;
    vec3 localE1=context.world ? position(t.y)-position(t.x):vertices[t.y].local.xyz-v.local.xyz;
    vec3 localE2=context.world ? position(t.z)-position(t.x):vertices[t.z].local.xyz-v.local.xyz;
    context.localDx=localE1*baryGradients[0].x+localE2*baryGradients[0].y;
    context.localDy=localE1*baryGradients[1].x+localE2*baryGradients[1].y;
    context.vertexColor=vertices[t.x].color*weights.x+vertices[t.y].color*weights.y+vertices[t.z].color*weights.z;
    uint rgba=floatBitsToUint(v.meta.w);
    context.entityColor=vec4(rgba&255u,(rgba>>8)&255u,(rgba>>16)&255u,rgba>>24)/255;
    if(!uniformTint && materials[id].layers[layerIndex].params.z==4) {
        vec3 worldE1=position(t.y)-position(t.x),worldE2=position(t.z)-position(t.x);
        vec3 world=position(t.x)+worldE1*bary.x+worldE2*bary.y;
        vec3 dn1=vertices[t.y].normal.xyz-v.normal.xyz,dn2=vertices[t.z].normal.xyz-v.normal.xyz;
        vec3 normal=v.normal.xyz+dn1*bary.x+dn2*bary.y;
        mat3 toLocal=context.world ? mat3(1):environmentMaterialBasis(worldE1,worldE2,localE1,localE2);
        vec3 viewer=observer-world;
        uv=environmentMaterialUV(normal,viewer,toLocal);
        gradients=mat2(
            environmentMaterialUV(normal+dn1*baryGradients[0].x+dn2*baryGradients[0].y,
                viewer-worldE1*baryGradients[0].x-worldE2*baryGradients[0].y,toLocal)-uv,
            environmentMaterialUV(normal+dn1*baryGradients[1].x+dn2*baryGradients[1].y,
                viewer-worldE1*baryGradients[1].x-worldE2*baryGradients[1].y,toLocal)-uv);
    }
    }
    vec4 sampled=layerSampleUV(id,layerIndex,uv,gradients,context);
    if(materials[id].layers[layerIndex].meta.y==8) {
        vec3 world=position(t.x)*weights.x+position(t.y)*weights.y+position(t.z)*weights.z;
        sampled.a*=clamp(length(observer-world)/max(materials[id].optical.w,1),0,1);
    }
    return sampled;
}
#ifdef PT_PROFILE_PASS
vec4 timedLayerSample(uint primitive,vec2 bary,int layerIndex,mat2 baryGradients,vec3 observer,bool uniformTint) {
    uint previous=profileEnter(2u);
    vec4 value=layerSample(primitive,bary,layerIndex,baryGradients,observer,uniformTint);
    profileEnter(previous);
    return value;
}
#define layerSample timedLayerSample
#endif
vec4 layerSample(uint primitive,vec2 bary,int layerIndex,mat2 baryGradients,vec3 observer) {
    return layerSample(primitive,bary,layerIndex,baryGradients,observer,false);
}
vec4 layerSample(uint primitive,vec2 bary,int layerIndex,vec3 observer) {
    return layerSample(primitive,bary,layerIndex,textureBarycentrics(primitive),observer);
}
#include "pt_material_layers.glsl"
#include "pt_portal.glsl"
#include "pt_reflective_shell.glsl"
bool passesAlpha(uint primitive, vec2 bary,vec3 observer) {
    Material m=materialProperties(triangleMaterials[primitive]&0xffffu);
    if (m.surface.w==0 && m.params.x!=1) return true;
    // Visibility/cutout acceptance retains its unfiltered coverage model. A
    // shaded hit's cone must not accidentally leak into another ray candidate.
    float alpha=layerSample(primitive,bary,materialBaseLayer(primitive),mat2(0),observer).a;
    if (m.params.x==1 && randomFloat()>=clamp(alpha,0,1)) return false;
    if (m.surface.w==0) return true;
    if (m.surface.w==1) return alpha>0;
    if (m.surface.w==2) return alpha<0.5;
    return alpha>=0.5;
}
#include "pt_glow_coverage.glsl"
vec3 filterTransmission(uint primitive,vec2 bary,mat2 gradients,vec3 observer) {
    vec3 color=clamp(linearColor(materialColor(primitive,bary,gradients,observer).rgb),0,1);
    return materials[triangleMaterials[primitive]&0xffffu].absorption.w!=0 ? 1-color:color;
}
vec2 surfaceProperties(uint primitive,vec2 bary) {
    if(portalIndex(primitive)!=0u) return vec2(1,0); // Composite guide, not a local mirror.
    Material m=materialProperties(triangleMaterials[primitive]&0xffffu);
    // ORM: R is baked occlusion (not used by physical transport), G roughness,
    // B metalness. These data channels must not undergo an sRGB conversion.
    return m.maps.z>=0 ? clamp(triangleTexture(m.maps.z,primitive,bary).gb,vec2(0.02,0),vec2(1)) : m.surface.yz;
}
struct Hit { float distance; uint primitive; vec2 bary; };
#ifdef PT_SOFTWARE_TRACE
#include "pt_software_query.glsl"
#define queryPrimitive(query,committed) ((committed) ? (query).committedPrimitive : (query).candidatePrimitive)
#else
// Only the world BLAS has a second geometry; custom indices address dynamic
// and first-person ranges. GLSL requires the committed operand to be literal.
#define queryPrimitive(query,committed) (rayQueryGetIntersectionInstanceCustomIndexEXT(query,committed) + \
    rayQueryGetIntersectionPrimitiveIndexEXT(query,committed) + \
    (rayQueryGetIntersectionGeometryIndexEXT(query,committed)==1 ? uint(lightSelection.y):0u))
#endif
bool trace(vec3 origin, vec3 direction, float minimum, float maximum,
    uint purpose, out Hit hit) {
    rayQueryEXT query;
    rayQueryInitializeEXT(query, sceneAS,
        gl_RayFlagsNoneEXT,
        purpose==3u ? 8u : (purpose==1u ? 1u : (purpose==2u ? 4u : 2u)),
        origin, minimum, direction, maximum);
    while(rayQueryProceedEXT(query)) {
        PT_COUNT(8u);
        if(rayQueryGetIntersectionTypeEXT(query,false)==gl_RayQueryCandidateIntersectionTriangleEXT) {
            PT_COUNT(9u);
            uint primitive=queryPrimitive(query,false);
            vec2 bary=rayQueryGetIntersectionBarycentricsEXT(query,false);
            // Sky polygons delimit the environment but must not occlude the sun.
            uint materialAndFlags=triangleMaterials[primitive];
            bool excluded=(purpose==1u && (materialAndFlags&0x80000000u)!=0u) ||
                (purpose==2u && (materialAndFlags&0x40000000u)!=0u);
            excluded=excluded || (materialAndFlags&0x08000000u)!=0u;
            bool firstPerson=(materialAndFlags&0x20000000u)!=0u;
            excluded=excluded || (purpose==3u ? !firstPerson:firstPerson);
            bool sky=materials[materialAndFlags&0xffffu].emission.z!=0;
            bool additive=materials[materialAndFlags&0xffffu].params.x==2;
            bool independentGlow=purpose!=2u && materials[materialAndFlags&0xffffu].maps.w==3;
            if (!excluded && (purpose!=2u || (!sky && !additive)) &&
                (independentGlow || passesAlpha(primitive,bary,origin)))
            {
                rayQueryConfirmIntersectionEXT(query);
                PT_COUNT(10u);
            }
        }
    }
    if(rayQueryGetIntersectionTypeEXT(query,true)==gl_RayQueryCommittedIntersectionNoneEXT)
        return false;
    hit.distance=rayQueryGetIntersectionTEXT(query,true);
    hit.primitive=queryPrimitive(query,true);
    hit.bary=rayQueryGetIntersectionBarycentricsEXT(query,true);
    return true;
}
#ifdef PT_PROFILE_PASS
bool timedTrace(vec3 origin,vec3 direction,float minimum,float maximum,uint purpose,out Hit hit) {
    uint previous=profileEnter(1u);
    PT_COUNT(1u);
    bool value=trace(origin,direction,minimum,maximum,purpose,hit);
    profileEnter(previous);
    return value;
}
#define trace timedTrace
#endif
// Keep a straight effect ray anchored at its original origin. The next float
// excludes the consumed intersection without a world-space gap that can erase
// a nearby surface. The camera near plane is applied only in that fixed frame.
float continuationT(float distance) {
    return uintBitsToFloat(floatBitsToUint(distance)+1u);
}
bool primaryHit(vec3 origin,vec3 direction,float minimum,float maximum,out Hit hit) {
    // Quake's first-person presentation layer has primary visibility priority,
    // but its pixels are fully ray-hit/material shaded. It is not a depth/color
    // copy from rasterization. The third-person model serves secondary rays.
    if(minimum>=pc.parameters.y) return false;
    // Do not clip the presentation query: even a weapon behind the fog event
    // has priority over world surfaces. In that case the fog event wins, not
    // world geometry that the original primary-hit rule deliberately ignored.
    if(lightCounts.z>0 && trace(origin,direction,max(0.001,minimum),pc.parameters.y,3u,hit))
        return hit.distance<=maximum;
    float worldMinimum=max(pc.originNear.w,minimum);
    return worldMinimum<maximum && trace(origin,direction,worldMinimum,maximum,1u,hit);
}
bool primaryHit(vec3 origin,vec3 direction,float minimum,out Hit hit) {
    return primaryHit(origin,direction,minimum,pc.parameters.y,hit);
}
bool primaryHit(vec3 origin,vec3 direction,out Hit hit) {
    return primaryHit(origin,direction,0,hit);
}
#include "pt_fog_query.glsl"
vec3 geometricNormal(uint primitive, out float area) {
    uvec3 t=triangle(primitive);
    vec3 crossEdges=cross(position(t.y)-position(t.x),position(t.z)-position(t.x));
    float lengthEdges=length(crossEdges);
    area=lengthEdges*0.5;
    return crossEdges/max(lengthEdges,0.000001);
}
#include "pt_decals.glsl"
vec3 shadingNormal(Hit hit, vec3 direction, out vec3 geometric) {
    uvec3 t=triangle(hit.primitive);
    float area;
    geometric=geometricNormal(hit.primitive,area);
    vec3 n=vertices[t.x].normal.xyz*(1-hit.bary.x-hit.bary.y)+
        vertices[t.y].normal.xyz*hit.bary.x+vertices[t.z].normal.xyz*hit.bary.y;
    n=dot(n,n)>0.00001 ? normalize(n) : geometric;
    // Follow the authored outward normal even when BSP winding is reversed.
    if(dot(geometric,n)<0) geometric=-geometric;
    Material m=materialProperties(triangleMaterials[hit.primitive]&0xffffu);
    if(m.maps.y>=0) {
        vec3 e1=position(t.y)-position(t.x), e2=position(t.z)-position(t.x);
        vec2 uv1=vertices[t.y].uv.xy-vertices[t.x].uv.xy, uv2=vertices[t.z].uv.xy-vertices[t.x].uv.xy;
        float determinant=uv1.x*uv2.y-uv1.y*uv2.x;
        if(abs(determinant)>0.0000001) {
            vec3 tangent=(e1*uv2.y-e2*uv1.y)/determinant;
            tangent-=n*dot(n,tangent);
            if(dot(tangent,tangent)>0.000001) {
                tangent=normalize(tangent);
                vec3 bitangent=normalize(cross(n,tangent))*sign(dot(cross(n,tangent),(e2*uv1.x-e1*uv2.x)/determinant));
                vec3 mapped=triangleTexture(m.maps.y,hit.primitive,hit.bary).xyz*2-1;
                mapped.xy*=m.optical.w;
                n=normalize(tangent*mapped.x+bitangent*mapped.y+n*max(mapped.z,0.001));
            }
        }
    }
    if(m.optical.z!=0 && m.maps.y<0) {
        vec3 world=position(t.x)*(1-hit.bary.x-hit.bary.y)+position(t.y)*hit.bary.x+position(t.z)*hit.bary.y;
        n=waterShadingNormal(n,world,rp.depthProjection.z);
    }
    if(dot(geometric,direction)>0) geometric=-geometric;
    if(dot(n,geometric)<0) n=-n;
    if(dot(n,-direction)<0.001) n=geometric;
    return n;
}
#ifdef PT_PROFILE_PASS
vec3 timedShadingNormal(Hit hit,vec3 direction,out vec3 geometric) {
    uint previous=profileEnter(2u);
    vec3 value=shadingNormal(hit,direction,geometric);
    profileEnter(previous);
    return value;
}
#define shadingNormal timedShadingNormal
#endif
vec3 emissionAt(uint primitive, vec2 bary, bool acceptedHit,vec3 observer,bool visibleFlash) {
    uint id=triangleMaterials[primitive]&0xffffu;
    Material m=materialProperties(id);
    if(m.emission.y<=0) return vec3(0);
    float alpha=(m.surface.w!=0 || m.params.x==1) ?
        layerSample(primitive,bary,materialBaseLayer(primitive),observer).a:1;
    bool rejectedBase=(m.surface.w==1 && alpha<=0) || (m.surface.w==2 && alpha>=0.5) ||
        (m.surface.w==3 && alpha<0.5);
    if(rejectedBase && m.maps.w!=3) return vec3(0);
    vec3 emission=vec3(0);
    vec4 composition=materials[id].composition;
    uint additiveMask=uint(composition.z);
    vec4 destination=vec4(0);
    for(int layer=0;layer<int(composition.x);++layer) {
        vec4 color=layerSample(primitive,bary,layer,observer);
        vec4 generators=materials[id].layers[layer].generators;
        // A depth-equal stage requires the cutout body to have written depth.
        // Unrestricted additive stages still glow in the body's empty areas.
        if(rejectedBase && (uint(generators.w)&0x20000u)!=0u) continue;
        if(!materialLayerAccepted(color,int(generators.z))) continue;
        vec4 sf,df;
        materialBlendFactors(color,destination,uint(generators.w),sf,df);
        bool additive=(additiveMask&(1u<<uint(layer)))!=0u;
        if(m.params.z==0 && layer==int(composition.y)) {
            // A surface light without explicit glow stages emits from its
            // backing image. Later frames/grilles are reflectance, not lamps.
            // Like materialColor, the base replaces a discarded lightmap.
            emission=linearColor(color.rgb);
        } else {
            vec3 retained=df.rgb;
            uint sourceBlend=uint(generators.w)&15u;
            uint destinationBlend=(uint(generators.w)>>4)&15u;
            // A non-emissive color filter modulates existing radiance even
            // when expressed as SRC=DST_COLOR, DST=ZERO. It does not add its
            // own radiance. Alpha masks remain linear coverage weights.
            if(!additive && sourceBlend==3u) retained+=color.rgb;
            if(!additive && sourceBlend==4u) retained-=color.rgb;
            if(!additive && (sourceBlend==3u || sourceBlend==4u ||
                destinationBlend==3u || destinationBlend==4u))
                retained=linearColor(clamp(retained,0,1));
            emission*=retained;
            if(additive) emission+=linearColor(color.rgb)*sf.rgb;
        }
        // A later opaque/alpha/filter layer covers earlier glow too. The
        // backing texture itself does not turn into an additive light source.
        destination=layer==int(composition.y) ? color:clamp(color*sf+destination*df,0,1);
    }
    // q3map_lightimage is a compiler proxy, not the visible emission image.
    // Animated displays keep their own UVs; opaque foregrounds block their
    // glow for camera hits AND sampled area-light contributions.
    return emission*m.emission.y*(!acceptedHit && m.params.x==1 ? alpha:1)*weaponEmissionScale(primitive,visibleFlash);
}
vec3 emissionAt(uint primitive,vec2 bary,bool acceptedHit,vec3 observer) {
    return emissionAt(primitive,bary,acceptedHit,observer,false);
}
#ifdef PT_PROFILE_PASS
vec3 timedEmissionAt(uint primitive,vec2 bary,bool acceptedHit,vec3 observer,bool visibleFlash) {
    uint previous=profileEnter(2u);
    vec3 value=emissionAt(primitive,bary,acceptedHit,observer,visibleFlash);
    profileEnter(previous);
    return value;
}
vec3 timedEmissionAt(uint primitive,vec2 bary,bool acceptedHit,vec3 observer) {
    return timedEmissionAt(primitive,bary,acceptedHit,observer,false);
}
#define emissionAt timedEmissionAt
#endif
vec3 visibility(vec3 origin,vec3 direction,float distance) {
    if(distance<=0.001) return vec3(1);
    rayQueryEXT query;
    rayQueryInitializeEXT(query,sceneAS,gl_RayFlagsTerminateOnFirstHitEXT,
        4u,origin,0.001,direction,distance);
    vec3 transmission=vec3(1);
    int filters=0;
    while(rayQueryProceedEXT(query)) {
        PT_COUNT(11u);
        if(rayQueryGetIntersectionTypeEXT(query,false)!=gl_RayQueryCandidateIntersectionTriangleEXT) continue;
        PT_COUNT(12u);
        uint primitive=queryPrimitive(query,false);
        uint flags=triangleMaterials[primitive], id=flags&0xffffu;
        if((flags&0x48000000u)!=0u || materials[id].emission.z!=0 || materials[id].params.x==2) continue;
        vec2 bary=rayQueryGetIntersectionBarycentricsEXT(query,false);
        if(!passesAlpha(primitive,bary,origin)) { PT_COUNT(14u); continue; }
        if(materials[id].params.x==4 && materials[id].optical.y<0) {
            if(++filters>=32) return vec3(0);
            PT_COUNT(15u);
            vec3 geometric;
            vec3 n=shadingNormal(Hit(0,primitive,bary),direction,geometric);
            vec3 r,t;
            reflectiveShellWeights(dielectricFresnel(dot(-direction,n),1,materials[id].optical.x),
                reflectiveShellAppearance(primitive,bary,origin),r,t);
            transmission*=t;
        } else if(materials[id].params.x==3) {
            // Multiplicative filters commute: one traversal visits them in any
            // order, instead of restarting a closest-hit query for every pane.
            if(++filters>=32) return vec3(0);
            PT_COUNT(15u);
            transmission*=filterTransmission(primitive,bary,mat2(0),origin);
        } else {
            rayQueryConfirmIntersectionEXT(query);
            PT_COUNT(13u);
        }
    }
    return rayQueryGetIntersectionTypeEXT(query,true)==gl_RayQueryCommittedIntersectionNoneEXT ?
        transmission*exp(-visibilityAbsorption*distance)*fogTransmittance(origin,direction,0,distance):vec3(0);
}
#ifdef PT_PROFILE_PASS
vec3 timedVisibility(vec3 origin,vec3 direction,float distance) {
#ifdef PT_SOFTWARE_PROFILE
    uint previous=profileEnter(profileCategory==9u ? 10u:12u);
#else
    uint previous=profileEnter(profileCategory==9u ? 10u:1u);
#endif
    if(previous==9u) { PT_COUNT(6u); }
    PT_COUNT(2u);
    vec3 value=visibility(origin,direction,distance);
    profileEnter(previous);
    return value;
}
#define visibility timedVisibility
#endif
vec3 basisSample(vec3 n, vec3 local) {
    vec3 tangent=normalize(cross(abs(n.z)<0.999 ? vec3(0,0,1):vec3(0,1,0),n));
    return tangent*local.x+cross(n,tangent)*local.y+n*local.z;
}
vec3 fresnel(vec3 f0,float cosine) {
    return f0+(1-f0)*pow(clamp(1-cosine,0,1),5);
}
float distribution(float nh,float a2) {
    float d=nh*nh*(a2-1)+1;
    return a2/max(PI*d*d,0.0000001);
}
float smith(float cosine,float a2) {
    return 2*cosine/max(cosine+sqrt(a2+(1-a2)*cosine*cosine),0.000001);
}
float specularProbability(vec3 albedo,float metallic) {
    return clamp(mix(0.2, max(albedo.r,max(albedo.g,albedo.b)),metallic),0.1,0.9);
}
vec3 evaluateBRDF(vec3 n,vec3 v,vec3 l,vec3 albedo,float roughness,float metallic,out float pdf) {
    float nv=max(dot(n,v),0), nl=max(dot(n,l),0);
    pdf=0;
    diffuseBRDF=vec3(0);
    if(nv<=0 || nl<=0) return vec3(0);
    vec3 h=normalize(v+l);
    float nh=max(dot(n,h),0), vh=max(dot(v,h),0.000001);
    float a=max(roughness*roughness,0.001), a2=a*a;
    float d=distribution(nh,a2);
    vec3 f=fresnel(mix(vec3(0.04),albedo,metallic),vh);
    float p=specularProbability(albedo,metallic);
    pdf=(1-p)*nl/PI+p*d*nh/(4*vh);
    diffuseBRDF=(1-f)*(1-metallic)*albedo/PI;
    return diffuseBRDF+f*d*smith(nv,a2)*smith(nl,a2)/max(4*nv*nl,0.000001);
}
vec3 sampleBRDF(vec3 n,vec3 v,vec3 albedo,float roughness,float metallic) {
    float phi=2*PI*randomFloat(), xi=randomFloat();
    if(randomFloat()<specularProbability(albedo,metallic)) {
        float a=max(roughness*roughness,0.001);
        float cosine=sqrt((1-xi)/max(1+(a*a-1)*xi,0.000001));
        float sine=sqrt(max(0,1-cosine*cosine));
        vec3 h=basisSample(n,vec3(sine*cos(phi),sine*sin(phi),cosine));
        return reflect(-v,h);
    }
    return basisSample(n,vec3(sqrt(xi)*cos(phi),sqrt(xi)*sin(phi),sqrt(1-xi)));
}
float powerWeight(float a,float b) { return a*a/max(a*a+b*b,0.000000001); }
#include "pt_brdf_reuse.glsl"
uint sampledEmitterIndex;
uint sampleEmitter() {
    float u=randomFloat(),target=u*lightSelection.x;
    uint low=0, high=lightCounts.x-1;
    if(emitterSearchControl.x!=0u) {
        // The same CDF and <= comparison below, within conservative bounds.
        // This preserves the exact emitter and random sequence, including ties.
        uint bucket=min(uint(u*64.0),63u), component=(bucket&1u)*2u;
        low=emitterSearchRanges[bucket>>1u][component];
        high=emitterSearchRanges[bucket>>1u][component+1u];
    }
    while(low<high) {
        uint mid=(low+high)/2;
        if(emitters[mid].cumulativePower<=target) low=mid+1;
        else high=mid;
    }
    sampledEmitterIndex=low;
    return emitters[low].primitive;
}
#include "pt_emitter_geometry.glsl"
float emitterPDF(uint primitive,float distanceSquared,float cosine) {
    float power=emissionPower(primitive);
    // Selection probability is area * power / total, divided by area
    // and converted from area measure to solid angle at the shading point.
    return power*distanceSquared/max(lightSelection.x*cosine,0.000001);
}
#include "pt_sky.glsl"
float pointAttenuation(uint light,vec3 direction,float distanceSquared) {
    float cone=1;
    if(pointCones[light].w>0) {
        float cosine=dot(pointCones[light].xyz,-direction);
        cone=smoothstep(pointCones[light].w,min(1,pointCones[light].w+0.02),cosine);
    }
    return pointPositions[light].w*cone/max(distanceSquared,1);
}
#include "pt_penumbra.glsl"
#if defined(PT_RR_GUIDES) || defined(PT_RR_TRACE) || defined(PT_RR_SPATIAL)
#include "pt_rr_sampling.glsl"
#endif
#ifdef PT_RR_COMBINED
#include "pt_rr_images.glsl"
#include "pt_rr_output.glsl"
#endif
#include "pt_fog_lighting.glsl"
void integrator(vec3 camera,vec3 direction) {
#ifdef PT_SOFTWARE_NRD
    softwareHitDistance=vec2(0);
    softwareDistancePending=false;
#endif
    PT_CATEGORY(0u);
    PT_COUNT(0u);
#ifdef PT_COMPACT_TRANSPORT
    resetCompactPath();
#else
    pathD=pathS=pathT=radianceD=radianceS=radianceT=radianceE=vec3(0); pathE=vec3(1);
#endif
    vec3 origin=camera;
#ifdef PT_COMPACT_MEDIA
    uint mediumRecords[4];
#else
    float mediumIOR[4]; vec3 mediumAbsorption[4];
#endif
    int mediumCount=0;
    if(rp.cameraMedium.x>1) {
#ifdef PT_COMPACT_MEDIA
        mediumRecords[0]=0xffffffffu; mediumCount=1;
#else
        mediumIOR[0]=rp.cameraMedium.x; mediumAbsorption[0]=rp.cameraMedium.yzw; mediumCount=1;
#endif
    }
    float previousPDF=0;
    int transparentLayers=0;
    float segmentDistance=0;
    float coneWidth=0,coneSpread=pixelConeSpread();
    for(int bounce=0;bounce<int(pc.sampling.y);++bounce) {
#ifdef PT_SOFTWARE_PROFILE
        SW_COUNT(31u);
#endif
        PT_CATEGORY(0u);
        Hit hit;
        float fogDistance; uint fogVolume;
        bool found=fogPathHit(origin,direction,segmentDistance,previousPDF==-2 ? 1:bounce,hit,fogDistance,fogVolume);
#ifdef PT_SOFTWARE_NRD
        if(softwareDistancePending && (!found || materialProperties(triangleMaterials[hit.primitive]&0xffffu).params.x<2)) {
            float distance=found ? hit.distance : pc.parameters.y;
            softwareHitDistance=vec2(any(greaterThan(pathD,vec3(0))) ? distance:0,
                any(greaterThan(pathS,vec3(0))) ? distance:0);
            softwareDistancePending=false;
        }
#endif
        PT_CATEGORY(2u);
        if(!found && fogVolume!=0xffffffffu) {
            PT_COUNT(4u);
            float traveled=max(fogDistance-segmentDistance,0);
            coneWidth+=traveled*coneSpread;
            if(mediumCount>0) attenuate(exp(-MEDIUM_ABSORPTION(mediumCount-1)*traveled));
            addIncident(fogVolumes[fogVolume].emission.rgb);
            vec3 fogWeight;
            if(!fogScatter(fogVolume,fogWeight)) break;
            attenuate(fogWeight);
            if(!any(greaterThan(fogVolumes[fogVolume].albedo.rgb,vec3(0)))) break;
            // A first volume scatter is diffuse transport; later events retain
            // the already selected reflection/transmission contribution class.
#ifdef PT_COMPACT_TRANSPORT
            if(pathClass==0u) pathClass=1u;
#else
            pathD+=pathE; pathE=vec3(0);
#endif
            vec3 fogPoint=origin+direction*fogDistance;
            visibilityAbsorption=mediumCount>0 ? MEDIUM_ABSORPTION(mediumCount-1):vec3(0);
            addIncident(fogLighting(fogPoint,bounce+1>=int(pc.sampling.y)));
            // Isotropic ambient fill also reaches surviving volume scatters;
            // absorption/scattering albedo and path attenuation already apply.
            if(skyEnvironment.y>0) addIncident(vec3(skyEnvironment.y));
            if(bounce+1>=int(pc.sampling.y)) break;
            origin=fogPoint; direction=fogDirection();
            coneSpread=max(coneSpread,1);
            previousPDF=1/(4*PI); segmentDistance=0;
            continue;
        }
        if(!found) {
            if(mediumCount>0) attenuate(exp(-MEDIUM_ABSORPTION(mediumCount-1)*max(pc.parameters.y-segmentDistance,0)));
            addIncident(environment(direction,coneSpread));
            break;
        }
        Material m=surfaceHitMaterial(hit.primitive,hit.bary,origin);
        PT_COUNT(3u);
        float traveled=max(hit.distance-segmentDistance,0);
        coneWidth+=traveled*coneSpread;
        setTextureFootprint(hit.primitive,direction,coneWidth);
        if(mediumCount>0) attenuate(exp(-MEDIUM_ABSORPTION(mediumCount-1)*traveled));
        if(m.emission.z!=0) {
            // Some legacy skies (notably blacksky) have SURF_SKY but no skyparms.
            // They retain their source texture, while all sky boundaries pass sun rays.
            vec3 sky=m.emission.z==2 ? linearColor(materialColor(hit.primitive,hit.bary,origin).rgb) :
                skyRadiance(triangleMaterials[hit.primitive]&0xffffu,direction,coneSpread);
            addIncident(sky);
            break;
        }
        vec3 world=origin+direction*hit.distance, geometric;
        if(portalIndex(hit.primitive)!=0u) {
            vec3 coating,transmission;
            portalCoating(hit.primitive,hit.bary,origin,coating,transmission);
            addIncident(coating);
            attenuate(transmission);
            if(++transparentLayers>=32 || !any(greaterThan(transmission,vec3(0.00001))) ||
                !portalRay(hit.primitive,world,origin,direction)) break;
            // The destination has its own medium; never carry local glass absorption
            // through a camera teleport. Fog volumes are queried at the new origin.
            mediumCount=0; previousPDF=-2; segmentDistance=0;
            --bounce;
            continue;
        }
        if(m.params.x==4) {
            // There is no next ray at the bounce limit. This interface emits
            // nothing, so sampling a discarded delta lobe cannot add radiance.
            if(bounce+1>=int(pc.sampling.y)) break;
            vec3 n=shadingNormal(hit,direction,geometric);
            uvec3 t=triangle(hit.primitive);
            vec3 outward=vertices[t.x].normal.xyz*(1-hit.bary.x-hit.bary.y)+
                vertices[t.y].normal.xyz*hit.bary.x+vertices[t.z].normal.xyz*hit.bary.y;
            bool entering=dot(outward,direction)<0;
            float etaI=mediumCount>0 ? MEDIUM_IOR(mediumCount-1):1;
            float etaT=entering ? m.optical.x : (mediumCount>1 ? MEDIUM_IOR(mediumCount-2):1);
            // A back face as the first boundary means the camera started inside.
            if(!entering && mediumCount==0 && m.optical.y==0) {
                etaI=m.optical.x;
                attenuate(exp(-m.absorption.rgb*hit.distance));
#ifdef PT_COMPACT_MEDIA
                mediumRecords[0]=triangleMaterials[hit.primitive]&0xffffu; mediumCount=1;
#else
                mediumIOR[0]=etaI; mediumAbsorption[0]=m.absorption.rgb; mediumCount=1;
#endif
            }
            bool shell=m.optical.y<0,thin=m.optical.y!=0;
            if(thin) etaT=m.optical.x;
            float reflectance=dielectricFresnel(dot(-direction,n),etaI,etaT);
            vec3 transmitted=refract(direction,n,etaI/etaT);
            if(dot(transmitted,transmitted)<0.000001) reflectance=1;
            // A finite parallel pane includes both interfaces and the geometric
            // series of internal reflections. Transmitted rays leave parallel,
            // but with the real lateral offset from their path inside the slab.
            vec3 paneTransmission=vec3(1), paneReflection=vec3(reflectance);
            float paneProbability=reflectance;
            if(shell) {
                reflectiveShellWeights(reflectance,reflectiveShellAppearance(hit.primitive,hit.bary,origin),
                    paneReflection,paneTransmission);
                paneProbability=max(paneReflection.r,max(paneReflection.g,paneReflection.b));
            } else if(m.optical.y>0 && reflectance<1) {
                float distance=m.optical.y/max(abs(dot(transmitted,geometric)),0.001);
                vec3 absorption=m.absorption.rgb;
                if(m.maps.w>0) absorption-=log(max(baseColor(hit.primitive,hit.bary,origin),vec3(0.01)))/max(m.optical.y,1);
                vec3 a=exp(-absorption*distance);
                vec3 denominator=max(1-reflectance*reflectance*a*a,vec3(0.000001));
                paneTransmission=(1-reflectance)*(1-reflectance)*a/denominator;
                paneReflection=vec3(reflectance)+(1-reflectance)*(1-reflectance)*reflectance*a*a/denominator;
                paneProbability=clamp(max(paneReflection.r,max(paneReflection.g,paneReflection.b)),0.001,0.999);
            }
            if(randomFloat()<(thin ? paneProbability:reflectance)) {
#ifdef PT_COMPACT_TRANSPORT
                compactReflect();
#else
                pathS+=pathE; pathE=vec3(0);
#endif
                if(thin) attenuate(paneReflection/max(paneProbability,0.000001));
                direction=reflect(direction,n);
                if(dot(direction,geometric)<=0) break;
                origin=world+geometric*pc.parameters.x;
            } else {
#ifdef PT_COMPACT_TRANSPORT
                compactTransmit();
#else
                pathT+=pathE; pathE=vec3(0);
#endif
                if(thin) {
                    attenuate(paneTransmission/max(1-paneProbability,0.000001));
                    // Equivalent zero-thickness exit on the surface plane avoids
                    // skipping real geometry behind a legacy single-polygon pane.
                    origin=shell ? world+direction*pc.parameters.x:
                        paneExit(world,direction,transmitted,geometric,m.optical.y,pc.parameters.x);
                } else {
                    if(dot(transmitted,geometric)>=0) break;
                    attenuate(vec3((etaI*etaI)/(etaT*etaT)));
                    direction=normalize(transmitted);
                    coneSpread*=etaI/etaT;
                    origin=world-geometric*pc.parameters.x;
                    if(entering) {
                        if(mediumCount>=4) break;
#ifdef PT_COMPACT_MEDIA
                        mediumRecords[mediumCount++]=triangleMaterials[hit.primitive]&0xffffu;
#else
                        mediumIOR[mediumCount]=etaT;
                        mediumAbsorption[mediumCount++]=m.absorption.rgb;
#endif
                    } else mediumCount=max(mediumCount-1,0);
                }
            }
            previousPDF=-1; segmentDistance=0;
            continue;
        }
        if(m.params.x==2 || m.params.x==3) {
            if(m.params.x==2) {
                float area;
                vec3 normal=geometricNormal(hit.primitive,area);
                float d=hit.distance;
                float lightPDF=emitterPDF(hit.primitive,d*d,abs(dot(normal,-direction)));
                addIncident(emissionAt(hit.primitive,hit.bary,true,origin,bounce==0 || previousPDF<0)*
                    (bounce==0 || previousPDF<0 ? 1:powerWeight(previousPDF,lightPDF)));
            } else attenuate(filterTransmission(hit.primitive,hit.bary,textureBarycentrics(hit.primitive),origin));
            if(++transparentLayers>=32) break;
            segmentDistance=hit.distance;
            --bounce;
            continue;
        }
        vec3 n=shadingNormal(hit,direction,geometric), v=-direction;
        vec3 albedo=baseColor(hit.primitive,hit.bary,origin);
        vec2 properties=surfaceProperties(hit.primitive,hit.bary);
        float roughness=properties.x, metallic=properties.y;
        addAmbient(albedo,metallic,skyEnvironment.y);
        if(m.emission.y>0) {
            float area;
            vec3 normal=geometricNormal(hit.primitive,area);
            float d=hit.distance;
            float lightPDF=emitterPDF(hit.primitive,d*d,abs(dot(normal,-direction)));
            float weight=bounce==0 || previousPDF<0 ? 1 : powerWeight(previousPDF,lightPDF);
            addIncident(emissionAt(hit.primitive,hit.bary,true,origin,bounce==0 || previousPDF<0)*weight);
        }
        vec3 start=world+geometric*pc.parameters.x;
        visibilityAbsorption=mediumCount>0 ? MEDIUM_ABSORPTION(mediumCount-1):vec3(0);
        // Next-event estimation samples the actual emitting triangles, including off-screen ones.
        PT_CATEGORY(3u);
        prepareHitBRDF(n,v,albedo,roughness,metallic);
#ifdef PT_UNIFIED_LIGHTS
#include "pt_light_loop.glsl"
#else
        if(lightCounts.x>0) {
            uint primitive=sampleEmitter();
            float root=sqrt(randomFloat());
            vec2 bary=vec2(root*(1-randomFloat()),0);
            bary.y=root-bary.x;
            vec3 cachedTarget,cachedNormal;
            float emitterPower;
            if(ptEmitterGeometry)
                sampledEmitterGeometry(primitive,sampledEmitterIndex,root,bary,cachedTarget,cachedNormal,emitterPower);
            uvec3 t=triangle(primitive);
            vec3 target=ptEmitterGeometry ? cachedTarget:
                position(t.x)*(1-root)+position(t.y)*bary.x+position(t.z)*bary.y;
            vec3 delta=target-start;
            float distance=length(delta),area;
            vec3 l=delta/max(distance,0.000001),ln=ptEmitterGeometry ? cachedNormal:geometricNormal(primitive,area);
            float nl=max(dot(n,l),0), lc=abs(dot(ln,-l));
            float lightPDF=ptEmitterGeometry ? emitterPower*(distance*distance)/max(lightSelection.x*lc,0.000001):
                emitterPDF(primitive,distance*distance,lc);
            if(nl>0 && dot(geometric,l)>0 && lc>0.0001 && distance>0.04) {
                vec3 transmittance=visibility(start,l,distance-0.02);
                if(any(greaterThan(transmittance,vec3(0)))) {
                PT_CATEGORY(5u);
                float pdf;
                vec3 brdf=evaluateHitBRDF(n,v,l,albedo,roughness,metallic,pdf);
                // No BSDF-hit technique is sampled after the final interaction;
                // assigning it MIS weight there would darken direct-only tests.
                float weight=bounce+1<int(pc.sampling.y) ? powerWeight(lightPDF,pdf) : 1;
                addDirect(brdf,emissionAt(primitive,bary,false,start)*nl/lightPDF*weight*transmittance);
                }
            }
        }
        PT_CATEGORY(7u);
        vec3 sun=normalize(pc.sunExposure.xyz);
        if(any(greaterThan(pc.sunRadiance.rgb,vec3(0)))) sun=sampleSunPenumbra(sun);
        if(dot(n,sun)>0 && dot(geometric,sun)>0 && any(greaterThan(pc.sunRadiance.rgb,vec3(0)))) {
            vec3 transmittance=visibility(start,sun,pc.parameters.y);
            if(any(greaterThan(transmittance,vec3(0)))) {
                PT_CATEGORY(5u);
                float pdf;
                vec3 brdf=evaluateHitBRDF(n,v,sun,albedo,roughness,metallic,pdf);
                addDirect(brdf,max(dot(n,sun),0)*pc.sunRadiance.rgb*transmittance);
            }
        }
        for(uint i=0;i<lightCounts.y;++i) {
            PT_CATEGORY(7u);
            vec3 delta=pointPositions[i].xyz-start;
            float distance=length(delta);
            vec3 l=delta/max(distance,0.000001);
            float shadowDistance;
            float lightScale=samplePointPenumbra(l,distance,shadowDistance);
            if(dot(n,l)<=0 || dot(geometric,l)<=0 || distance<=0.04) continue;
            vec3 transmittance=visibility(start,l,shadowDistance-0.02);
            if(any(greaterThan(transmittance,vec3(0)))) {
                PT_CATEGORY(5u);
                float pdf, intensity=pointPositions[i].w*pointPositions[i].w/max(distance*distance,1);
                vec3 brdf=evaluateHitBRDF(n,v,l,albedo,roughness,metallic,pdf);
                addDirect(brdf,dot(n,l)*pointColors[i].rgb*intensity*lightScale*transmittance);
            }
        }
        // Full-support spatial proposals, then directional RIS. The uniform
        // mixture preserves every light across doors, reflections and cell edges.
        PT_CATEGORY(4u);
        float totalWeight=0, selectedWeight=0;
        uint selected=0;
        uint cell=lightCell(start);
        uint candidateCount=min(lightCounts.w,8u);
        for(uint candidate=0;candidate<candidateCount;++candidate) {
            float proposalPDF;
            uint i=32+proposedLight(cell,randomFloat(),proposalPDF);
            vec3 delta=pointPositions[i].xyz-start;
            float distanceSquared=dot(delta,delta);
            vec3 l=delta*inversesqrt(max(distanceSquared,0.000001));
            // This is the SAME rejection as below. Its inputs are ready before
            // color/cone loads and attenuation, none of which consume RNG.
            if(skyEnvironment.w<=0 && ptMapLightCull && dot(geometric,l)<=0) continue;
            float weight=dot(pointColors[i].rgb,vec3(0.2126,0.7152,0.0722))*pointAttenuation(i,l,distanceSquared)*max(dot(n,l),0);
            // A finite source can cross the horizon or spotlight cone even
            // when its center cannot contribute. Preserve proposal support.
            if(skyEnvironment.w>0) weight=dot(pointColors[i].rgb,vec3(0.2126,0.7152,0.0722))*
                pointPositions[i].w/max(distanceSquared,1);
            if((skyEnvironment.w<=0 && !ptMapLightCull && dot(geometric,l)<=0) || weight<=0) continue;
            float reservoirWeight=weight/max(proposalPDF,0.00000001);
            totalWeight+=reservoirWeight;
            if(randomFloat()*totalWeight<reservoirWeight) { selected=i; selectedWeight=weight; }
        }
        PT_CATEGORY(7u);
        if(selectedWeight>0) {
            vec3 delta=pointPositions[selected].xyz-start;
            float distance=length(delta);
            vec3 l=delta/max(distance,0.000001);
            float shadowDistance;
            float lightScale=samplePointPenumbra(l,distance,shadowDistance);
            if(distance>0.04 && (skyEnvironment.w<=0 || (dot(n,l)>0 && dot(geometric,l)>0))) {
                vec3 transmittance=visibility(start,l,shadowDistance-0.02);
                if(any(greaterThan(transmittance,vec3(0)))) {
                PT_CATEGORY(5u);
                float pdf;
                vec3 incident=pointColors[selected].rgb*pointAttenuation(selected,l,distance*distance);
                vec3 brdf=evaluateHitBRDF(n,v,l,albedo,roughness,metallic,pdf);
                addDirect(brdf,dot(n,l)*incident*totalWeight/selectedWeight/float(candidateCount)*lightScale*transmittance);
                }
            }
        }
#endif
        // Direct illumination/emission above still use the final interaction.
        // Avoid sampling/evaluating a continuation that cannot be traced.
        PT_CATEGORY(6u);
        if(bounce+1>=int(pc.sampling.y)) break;
        vec3 next=sampleHitBRDF(n,v,albedo,roughness,metallic);
        if(dot(next,geometric)<=0) break;
        float pdf;
        vec3 brdf=evaluateHitBRDF(n,v,next,albedo,roughness,metallic,pdf);
        if(pdf<=0.0000001) break;
        float factor=max(dot(n,next),0)/pdf;
#ifdef PT_COMPACT_TRANSPORT
        compactScatter(brdf,factor);
#else
        pathD=(pathD*brdf+pathE*diffuseBRDF)*factor;
        pathS=(pathS*brdf+pathE*max(brdf-diffuseBRDF,vec3(0)))*factor;
        pathT*=brdf*factor;
        pathE=vec3(0);
#endif
#ifdef PT_SOFTWARE_NRD
        if(bounce==0) softwareDistancePending=true;
#endif
        previousPDF=pdf;
        if(bounce>=2) {
#ifdef PT_COMPACT_TRANSPORT
            vec3 throughput=compactThroughput();
#else
            vec3 throughput=pathD+pathS+pathT;
#endif
            float survival=clamp(max(throughput.r,max(throughput.g,throughput.b)),0.05,0.95);
            if(randomFloat()>survival) break;
            attenuate(vec3(1/survival));
        }
        origin=start; direction=next;
        segmentDistance=0;
    }
    PT_CATEGORY(0u);
}
vec3 toneMap(vec3 radiance) {
    vec3 x=max(radiance*pc.sunExposure.w,vec3(0));
    vec3 mapped=clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0,1);
    return pow(mapped,vec3(1.0/2.2));
}
vec3 primaryDirection(vec2 pixel,ivec2 size) {
    vec2 ndc=pixel/vec2(size)*2-1;
    return normalize(pc.forwardFar.xyz +
        pc.rightProjection.xyz*((ndc.x+pc.parameters.z)/pc.rightProjection.w)+
        pc.upProjection.xyz*((ndc.y+pc.parameters.w)/pc.upProjection.w));
}
#ifdef PT_GUIDE_PASS
#include "pt_guide_helpers.glsl"
#ifdef PT_RR_GUIDES
#include "pt_rr_images.glsl"
#include "pt_rr_material.glsl"
#endif
void writeGuide(uint index,vec3 direction) {
#ifdef PT_RR_GUIDES
    ivec2 rrSize=imageSize(rrDiffuse), rrPixel=ivec2(int(index)%rrSize.x,int(index)/rrSize.x);
    imageStore(rrDiffuse,rrPixel,vec4(0));
    imageStore(rrSpecular,rrPixel,vec4(0));
    imageStore(rrNormalRoughness,rrPixel,vec4(-direction,1));
    imageStore(rrHitDistance,rrPixel,vec4(0));
    if(pc.sunRadiance.w>0) {
        rrSurfaces[index]=RrSurface(vec4(0),vec4(0));
        rrTemporal[index]=vec4(0);
        if(pc.sunRadiance.w>=2) rrAdaptive[index]=vec4(0,0,0,pc.sampling.x);
    }
#endif
    Guide g;
    g.positionDepth=vec4(0,0,0,-1);
    g.normalMaterial=vec4(0);
    g.albedoRoughness=vec4(0);
    vec4 oldPosition=vec4(0), oldNormal=vec4(0);
    vec4 shading=vec4(0);
    vec3 reprojectionOffset=direction;
    float depth=1;
    ReflectionGuide reflected=ReflectionGuide(vec4(0),vec4(0)), oldReflected=reflected;
    TransmissionGuide transmittedGuide=TransmissionGuide(vec4(0),uvec4(0));
    TransmissionMotion transmittedMotion=TransmissionMotion(vec4(0),vec4(0),vec4(0));
    vec4 lightingDelta=vec4(0);
    Hit hit;
    bool found=false, reactive=false;
    float traveled=0;
    // Effects are not opaque depth. Follow straight additive/filter layers to
    // the underlying surface for depth/motion, while refusing native radiance
    // history wherever a visible effect changes the composite. Dielectrics
    // retain their interface guide; reflected/transmitted light is specular.
    for(int layer=0;layer<32;++layer) {
        if(!primaryHit(pc.originNear.xyz,direction,traveled>0 ? continuationT(traveled):0,hit)) break;
        Material material=surfaceHitMaterial(hit.primitive,hit.bary,pc.originNear.xyz);
        // Textured skies have infinite depth and rotation-only motion, not the
        // depth/parallax of the BSP polygons that mark their openings.
        if(material.emission.z!=0) break;
        if(material.params.x!=2 && material.params.x!=3) {
            found=true;
            break;
        }
#ifdef PT_RR_GUIDES
        reactive=true; // Retain full sampling under effects; no native history query.
#else
        if(material.params.x==3 || any(greaterThan(emissionAt(hit.primitive,hit.bary,true,pc.originNear.xyz,true),vec3(0.0001))))
            reactive=true;
#endif
        traveled=hit.distance;
    }
    if(found) {
        reactive=reactive || portalIndex(hit.primitive)!=0u ||
            fogTransmittance(pc.originNear.xyz,direction,0,hit.distance)<0.999;
        uint id=triangleMaterials[hit.primitive]&0xffffu;
        Material m=materialProperties(id);
        vec3 world=pc.originNear.xyz+direction*hit.distance;
        setTextureFootprint(hit.primitive,direction,hit.distance*pixelConeSpread());
        float viewDepth=dot(world-pc.originNear.xyz,pc.forwardFar.xyz);
        depth=clamp(-rp.depthProjection.x+rp.depthProjection.y/max(viewDepth,0.001),0,1);
        uvec3 t=triangle(hit.primitive);
        uint object=uint(vertices[t.x].normal.w);
        vec3 geometric;
        vec3 n=shadingNormal(hit,direction,geometric);
        vec2 properties=surfaceProperties(hit.primitive,hit.bary);
#ifdef PT_SOFTWARE_NRD
        softwareMetadata[index].zw=vec2(properties.y,m.params.x==4 ? 1:0);
#endif
#ifndef PT_RR_GUIDES
        visibilityAbsorption=rp.cameraMedium.x>1 ? rp.cameraMedium.yzw:vec3(0);
        lightingDelta=changedLighting(hit,world,direction,n,geometric,properties);
        shading=vec4(n,properties.x);
#endif
        uint guideID=id | (object<<16);
        oldPosition=vec4(world,1);
        oldNormal=vec4(geometric,uintBitsToFloat(guideID));
        if(object!=0u) {
            vec4 a=vertices[t.x].previous, b=vertices[t.y].previous, c=vertices[t.z].previous;
            oldPosition=vec4(a.xyz*(1-hit.bary.x-hit.bary.y)+b.xyz*hit.bary.x+c.xyz*hit.bary.y,
                min(a.w,min(b.w,c.w)));
            vec3 previousGeometric=cross(b.xyz-a.xyz,c.xyz-a.xyz);
            float magnitude=length(previousGeometric);
            if(magnitude<0.000001) oldPosition.w=0;
#ifndef PT_RR_GUIDES
            previousGeometric/=max(magnitude,0.000001);
            if(dot(previousGeometric,oldPosition.xyz-rp.previousOrigin.xyz)>0) previousGeometric=-previousGeometric;
            oldNormal.xyz=previousGeometric;
#endif
        }
        // Legacy untracked objects get camera motion only; native reconstruction
        // rejects their history. Built-in tracked models use actual prior vertices.
        reprojectionOffset=(oldPosition.w>0 ? oldPosition.xyz:world)-rp.previousOrigin.xyz;
#ifdef PT_RR_GUIDES
        vec3 albedo=baseColor(hit.primitive,hit.bary,pc.originNear.xyz);
        vec3 f0=mix(vec3(0.04),albedo,properties.y);
        vec3 diffuseAlbedo=albedo*(1-properties.y);
        float roughness=properties.x;
        if(m.params.x==4) {
            float eta=max(m.optical.x,1.001);
            f0=vec3(pow((eta-1)/(eta+1),2));
            diffuseAlbedo=vec3(0);
            roughness=0;
        }
        imageStore(rrDiffuse,rrPixel,vec4(clamp(diffuseAlbedo,0,1),1));
        imageStore(rrSpecular,rrPixel,vec4(rrSpecularAlbedo(f0,roughness,dot(n,-direction)),1));
        imageStore(rrNormalRoughness,rrPixel,vec4(n,roughness));
        // Independent reflection guide, never replace physical surface depth
        // or motion with virtual geometry. One deterministic reflection query
        // avoids adding per-path state to the measured transport shader.
        Hit specHit;
        vec3 specDirection=reflect(direction,n);
        float specDistance=0;
        if(dot(specDirection,geometric)>0 && trace(world+geometric*pc.parameters.x,specDirection,0.001,pc.parameters.y,0u,specHit))
            specDistance=specHit.distance;
        imageStore(rrHitDistance,rrPixel,vec4(specDistance));
        if(pc.sunRadiance.w>0) rrGuideSampling(index,world,n,roughness,guideID+1u,oldPosition,
            !reactive && m.params.x==0 && m.params.w==0 && object==0u &&
            roughness>=0.35 && properties.y<0.3 && !baseColorHasDecal);
#else
        if(!reactive && m.emission.z==0 && m.emission.y<=0 && (m.params.x==0 || m.params.x==4)) {
            g.positionDepth=vec4(world,hit.distance);
            // Identity is opaque bits, not a floating-point number. All consumers
            // compare via floatBitsToUint; even NaN bit patterns retain identity.
            g.normalMaterial=vec4(geometric,uintBitsToFloat(guideID));
            g.albedoRoughness=vec4(baseColor(hit.primitive,hit.bary,pc.originNear.xyz),properties.x);
            if(baseColorHasDecal) {
                // Decal submissions have no stable previous color/lifetime ID.
                // Preserve raw radiance here; an invalid old position also
                // rejects stale painted history on the first frame after removal.
                g.positionDepth.w=-abs(hit.distance);
                oldPosition.w=0;
            }
            // Texture/UV/color animation changes radiance without vertex motion.
            // Procedural water's optical transport does not use legacy UV/color
            // animation. Its wave is explicitly replayed at the previous time.
            bool proceduralWater=m.params.x==4 && m.optical.y==0 && m.optical.z!=0 && m.maps.y<0;
            if(m.params.w!=0 && !proceduralWater) oldPosition.w=0;
            // A virtual reflected surface is well-defined for a tracked, smooth
            // metal/glass plane. Water uses a real target plus an inverse
            // wave solve below, not a planar virtual-image approximation.
            if(oldPosition.w>0 && ((m.params.x==0 && properties.x<=0.04 && properties.y>=0.99) ||
                (rp.options.x!=0 && m.params.x==4 && m.optical.z==0 && m.maps.y<0)) &&
                dot(n,geometric)>0.9999 && m.params.w==0) {
                Hit secondary;
                vec3 reflectedDirection=reflect(direction,n);
                vec3 rayStart=world+geometric*pc.parameters.x;
                if(trace(rayStart,reflectedDirection,0.001,pc.parameters.y,0u,secondary)) {
                    Material targetMaterial=materialProperties(triangleMaterials[secondary.primitive]&0xffffu);
                    if(targetMaterial.params.x==0 && targetMaterial.params.w==0 && targetMaterial.emission.z==0) {
                        vec3 target=rayStart+reflectedDirection*secondary.distance, targetGeometric;
                        setTextureFootprint(secondary.primitive,reflectedDirection,
                            (hit.distance+secondary.distance)*pixelConeSpread());
                        vec3 targetNormal=shadingNormal(secondary,reflectedDirection,targetGeometric);
                        reflected=hitCorrespondence(secondary,target,reflectedDirection,false);
                        vec3 oldDirection=reflect(normalize(oldPosition.xyz-rp.previousOrigin.xyz),oldNormal.xyz);
                        oldReflected=hitCorrespondence(secondary,target,oldDirection,true);
                        reflected.position.xyz=mirrorPoint(reflected.position.xyz,world,n);
                        // Both the target and mirror can move. Reconstruct the
                        // virtual target using the mirror's previous plane.
                        oldReflected.position.xyz=mirrorPoint(oldReflected.position.xyz,oldPosition.xyz,oldNormal.xyz);
                        reflected.normal.xyz=reflect(reflected.normal.xyz,n);
                        oldReflected.normal.xyz=reflect(oldReflected.normal.xyz,oldNormal.xyz);
                        // DLSS has one motion/depth pair, unlike our split
                        // histories. Do not assign reflected motion to the
                        // transmitted image on a mixed dielectric pixel.
                        if(m.params.x==0) {
                            reprojectionOffset=(oldReflected.position.w>0 ? oldReflected.position.xyz:reflected.position.xyz)-rp.previousOrigin.xyz;
                            float virtualDepth=dot(reflected.position.xyz-pc.originNear.xyz,pc.forwardFar.xyz);
                            depth=clamp(-rp.depthProjection.x+rp.depthProjection.y/max(virtualDepth,0.001),0,1);
                        }
                        vec4 targetDelta=changedLighting(secondary,target,reflectedDirection,targetNormal,
                            targetGeometric,surfaceProperties(secondary.primitive,secondary.bary));
                        lightingDelta.zw+=targetDelta.xy+targetDelta.zw;
                    }
                }
            }
            vec3 authored=vertices[t.x].normal.xyz*(1-hit.bary.x-hit.bary.y)+
                vertices[t.y].normal.xyz*hit.bary.x+vertices[t.z].normal.xyz*hit.bary.y;
            bool flatInterface=dot(authored,authored)>0.000001 && abs(dot(normalize(authored),geometric))>0.9999;
            if(rp.options.x!=0 && proceduralWater) {
                // A missed/unsupported reflected target must NOT fall back to
                // surface-motion history: a moving wave changes that image.
                reflected.position.w=-1;
                bool entering=dot(authored,direction)<0;
                vec3 start,outgoing;
                Hit secondary;
                if(flatInterface && interfaceReflection(world,pc.originNear.xyz,geometric,true,
                    entering,rp.depthProjection.z,pc.parameters.x,start,outgoing) &&
                    trace(start,outgoing,0.001,pc.parameters.y,0u,secondary)) {
                    Material targetMaterial=materialProperties(triangleMaterials[secondary.primitive]&0xffffu);
                    if(targetMaterial.params.x==0 && targetMaterial.params.w==0 && targetMaterial.emission.z==0) {
                        vec3 target=start+outgoing*secondary.distance;
                        reflected=hitCorrespondence(secondary,target,outgoing,false);
                        reflected.position.w=2; // REAL target; never project it as a virtual mirror.
                        ReflectionGuide oldTarget=hitCorrespondence(secondary,target,outgoing,true);
                        vec3 previousPoint,oldStart,oldOutgoing;
                        if(oldPosition.w>0 && oldTarget.position.w>0 && rp.jitterHistoryReset.w==0 &&
                            previousReflectionPoint(oldPosition.xyz,oldNormal.xyz,oldTarget.position.xyz,
                                entering,previousPoint) &&
                            interfaceReflection(previousPoint,rp.previousOrigin.xyz,oldNormal.xyz,true,
                                entering,rp.depthProjection.w,pc.parameters.x,oldStart,oldOutgoing)) {
                            oldTarget=hitCorrespondence(secondary,target,oldOutgoing,true);
                            oldReflected.position=oldTarget.position;
                            oldReflected.normal=vec4(previousPoint,packReflectionNormal(oldTarget.normal.xyz));
                        }
                        // The target's local lighting can change even when the
                        // reflection correspondence is exact. Keep that rejection.
                        setTextureFootprint(secondary.primitive,outgoing,
                            (hit.distance+secondary.distance)*pixelConeSpread());
                        vec3 targetGeometric;
                        vec3 targetNormal=shadingNormal(secondary,outgoing,targetGeometric);
                        vec4 targetDelta=changedLighting(secondary,target,outgoing,targetNormal,
                            targetGeometric,surfaceProperties(secondary.primitive,secondary.bary));
                        lightingDelta.zw+=targetDelta.xy+targetDelta.zw;
                    }
                }
                // Mixed reflected/transmitted pixels keep PHYSICAL interface
                // depth/motion for DLSS. Only native split history uses this solve.
            }
            if(rp.options.x!=0 && m.params.x==4 && pc.sampling.y>=2)
                transmissionPathGuide(hit,world,direction,transmittedGuide,transmittedMotion);
        }
        ivec2 size=imageSize(outputColor), pixel=ivec2(int(index)%size.x,int(index)/size.x);
        if(rp.options.z==4) imageStore(outputColor,pixel,vec4(n*0.5+0.5,1));
        if(rp.options.z==5) imageStore(outputColor,pixel,vec4(properties,0,1));
        if(rp.options.z==6) imageStore(outputColor,pixel,vec4(m.params.x==4 ? vec3(m.optical.x/3,m.optical.y>0 ? 1:0,m.optical.z):vec3(0),1));
#endif
    }
#ifndef PT_RR_GUIDES
    guidePositions[index]=g.positionDepth;
    guideNormals[index]=g.normalMaterial;
    guideAlbedos[index]=g.albedoRoughness;
    motionPositions[index]=oldPosition;
    motionNormals[index]=oldNormal;
    shadingGuides[index]=shading;
    reflectionGuides[index]=reflected;
    reflectionMotion[index]=oldReflected;
    lightChange[index]=lightingDelta;
    transmissionGuides[index]=transmittedGuide;
    // Invalid guides are the validity mask: no consumer reads their motion.
    // Avoid a full 48-byte write for every ordinary opaque/sky pixel.
    if(transmittedGuide.identity.w>0u) transmissionMotions[index]=transmittedMotion;
#endif
    ivec2 size=imageSize(outputColor), pixel=ivec2(int(index)%size.x,int(index)/size.x);
    float previousDepth=dot(reprojectionOffset,rp.previousForward.xyz);
    vec2 motion=vec2(0);
    if(rp.jitterHistoryReset.w==0 && previousDepth>0.001) {
        vec2 previousNdc=vec2(dot(reprojectionOffset,rp.previousRight.xyz)*rp.previousRight.w,
            dot(reprojectionOffset,rp.previousUp.xyz)*rp.previousUp.w)/previousDepth;
        vec2 currentNdc=(vec2(pixel)+0.5)/vec2(size)*2-1+pc.parameters.zw;
        // Previous minus current, normalized UV units, with projection jitter removed.
        motion=(previousNdc-currentNdc)*0.5;
    }
    imageStore(outputMotion,pixel,vec4(motion,0,0));
    imageStore(outputDepth,pixel,vec4(depth));
}
#endif
#ifdef PT_RR_SPATIAL
void main() {
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy),size=imageSize(outputColor);
    if(any(greaterThanEqual(pixel,size))) return;
    uint index=uint(pixel.y*size.x+pixel.x);
    vec4 r=vec4(0); float sum=0;
    RrSurface surface=rrSurfaces[index];
    if((uint(pc.sunRadiance.w)&1u)!=0u && floatBitsToUint(surface.position.w)!=0u) {
        rrRandomState=sampleHash(index+uint(pc.sampling.z)*2891336453u+197u);
        rrMerge(r,sum,rrTemporal[index],surface.position.xyz,surface.normal.xyz,10);
        // Read only the completed temporal pass, never another spatial output.
        // Spatial results do NOT feed the next frame's temporal reservoir.
        for(int k=0;k<2;++k) {
            ivec2 delta=ivec2(floor(vec2(rrRandom(),rrRandom())*9))-4;
            ivec2 p=pixel+delta;
            if(any(lessThan(p,ivec2(0))) || any(greaterThanEqual(p,size)) || all(equal(delta,ivec2(0)))) continue;
            uint other=uint(p.y*size.x+p.x);
            float footprint=max(0.05,length(surface.position.xyz-pc.originNear.xyz)*pixelConeSpread());
            if(rrSameSurface(surface,rrSurfaces[other],8*footprint))
                rrMerge(r,sum,rrTemporal[other],surface.position.xyz,surface.normal.xyz,10);
        }
        if(r.z>0) r.y=rrNormalize(sum,r.z,rrTarget(uint(r.x),surface.position.xyz,surface.normal.xyz));
    }
    rrSpatial[index]=r;
}
#elif defined(PT_STAGED_PASS)
#include "pt_staged.glsl"
#elif defined(PT_PARALLEL_SAMPLES)
#include "pt_parallel_samples.glsl"
#else
void main() {
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy), size=imageSize(outputColor);
    if(any(greaterThanEqual(pixel,size))) return;
#ifdef PT_PROFILE_PASS
    profileBegin(uvec2(size),uint(pc.sampling.z));
#ifdef PT_SOFTWARE_PROFILE
    if(!profileEnabled) return;
#endif
#endif
    uint index=uint(pixel.y*size.x+pixel.x);
    samplePixel=index; sampleNumber=uint(pc.sampling.z); sampleDimension=0;
    sampleCoordinate=uvec2(pixel); sampleTile=sampleHash((uint(pixel.x)>>6)+4099u*(uint(pixel.y)>>6));
    rng=(index+1u)*747796405u+(uint(pc.sampling.z)+1u)*2891336453u;
    if(rng==0) rng=1;
#ifdef PT_GUIDE_PASS
#ifdef PT_SOFTWARE_NRD
    softwareMetadata[index]=vec4(0,0,0,1);
#endif
#ifndef PT_RR_GUIDES
    imageStore(outputColor,pixel,vec4(0,0,0,1));
#endif
    writeGuide(index,primaryDirection(vec2(pixel)+0.5,size));
#else
#ifdef PT_RR_COMBINED
    vec3 combined=vec3(0);
#else
    vec3 diffuse=vec3(0), reflection=vec3(0), transmitted=vec3(0), emission=vec3(0);
#endif
    float sampleCount=pc.sampling.x;
#ifdef PT_SOFTWARE_NRD
    vec2 softwareDistances=vec2(0,3.40282347e+38);
    float softwareDiffuseCount=0;
#endif
#ifdef PT_RR_TRACE
    if(pc.sunRadiance.w>=2) sampleCount=clamp(rrAdaptive[index].w,1,pc.sampling.x);
#endif
    for(int sampleIndex=0;sampleIndex<int(sampleCount);++sampleIndex) {
#ifdef PT_RR_TRACE
        rrPathSample=uint(sampleIndex);
#endif
        sampleNumber=uint(pc.sampling.z)*uint(pc.sampling.x)+uint(sampleIndex);
        sampleDimension=0;
        vec2 jitter=vec2(randomFloat(),randomFloat())-0.5;
        vec3 direction=primaryDirection(vec2(pixel)+0.5+jitter,size);
        integrator(pc.originNear.xyz,direction);
#ifdef PT_SOFTWARE_NRD
        if(softwareHitDistance.x>0) { softwareDistances.x+=softwareHitDistance.x; softwareDiffuseCount+=1; }
        if(softwareHitDistance.y>0) softwareDistances.y=min(softwareDistances.y,softwareHitDistance.y);
#endif
#ifdef PT_RR_COMBINED
        // Reject a nonfinite path before it can contaminate the pixel average.
        if(!any(isnan(rrRadiance)) && !any(isinf(rrRadiance))) combined+=rrRadiance;
#else
        if(!any(isnan(radianceD)) && !any(isinf(radianceD))) diffuse+=radianceD;
        if(!any(isnan(radianceS)) && !any(isinf(radianceS))) reflection+=radianceS;
        if(!any(isnan(radianceT)) && !any(isinf(radianceT))) transmitted+=radianceT;
        if(!any(isnan(radianceE)) && !any(isinf(radianceE))) emission+=radianceE;
#endif
    }
#ifdef PT_SOFTWARE_PROFILE
    // Keep shading live for measurement, but never overwrite production
    // radiance, NRD metadata, image output or temporal history during replay.
    profileSignal=vec4((diffuse+reflection+transmitted+emission)/sampleCount,sampleCount);
    profileEnd();
    return;
#endif
#ifdef PT_RR_COMBINED
    rrStoreNoisy(pixel,index,combined/sampleCount);
#else
    diffuse/=sampleCount; reflection/=sampleCount; transmitted/=sampleCount; emission/=sampleCount;
#ifdef PT_SOFTWARE_NRD
    softwareMetadata[index].xy=vec2(softwareDistances.x/max(softwareDiffuseCount,1),
        softwareDistances.y==3.40282347e+38 ? 0:softwareDistances.y);
#endif
    float history=pc.sampling.w;
    if(history>0) {
        diffuse=(accumulated[index].rgb*history+diffuse)/(history+1);
        reflection=(specular[index].rgb*history+reflection)/(history+1);
        transmitted=(transmission[index].rgb*history+transmitted)/(history+1);
        emission=(visibleEmission[index].rgb*history+emission)/(history+1);
    }
    accumulated[index]=vec4(diffuse,history+1);
    specular[index]=vec4(reflection,history+1);
    transmission[index]=vec4(transmitted,history+1);
    visibleEmission[index]=vec4(emission,history+1);
#ifndef PT_RR_TRACE
    vec3 result=rp.options.z==1 ? diffuse : (rp.options.z==2 ? reflection+transmitted : (rp.options.z==3 ? emission : diffuse+reflection+transmitted+emission));
    if(rp.options.z<4) imageStore(outputColor,pixel,vec4(toneMap(result),1));
#endif
#endif // PT_RR_COMBINED
#endif
#ifdef PT_PROFILE_PASS
    profileEnd();
#endif
}
#endif
