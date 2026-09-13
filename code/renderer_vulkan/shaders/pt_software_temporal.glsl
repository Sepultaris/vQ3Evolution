// Software-mode fork: keep pt_temporal.comp and the mode-2 payload unchanged.
#include "pt_reflection_motion.glsl"
#include "pt_transmission_guide.glsl"
// Camera and explicit object/vertex correspondence of path-hit radiance.
layout(local_size_x=8, local_size_y=8) in;
layout(binding=3, rgba8) uniform writeonly image2D outputColor;
layout(binding=11, std430) readonly buffer Radiance { vec4 accumulated[]; };
layout(binding=12, std430) readonly buffer Positions { vec4 positions[]; };
layout(binding=13, std430) readonly buffer Normals { vec4 normals[]; };
layout(binding=14, std430) readonly buffer Albedos { vec4 albedos[]; };
layout(binding=17, std430) readonly buffer Reprojection {
    vec4 previousOrigin, previousForward, previousRight, previousUp;
    vec4 jitterHistoryReset, options, depthProjection, cameraMedium;
} rp;
layout(binding=18, std430) readonly buffer History { vec4 history[]; };
layout(binding=19, std430) readonly buffer PreviousPositions { vec4 previousPositions[]; };
layout(binding=20, std430) readonly buffer PreviousNormals { vec4 previousNormals[]; };
layout(binding=21, std430) writeonly buffer Resolved { vec4 resolved[]; };
layout(binding=22, std430) readonly buffer MotionPositions { vec4 motionPositions[]; };
layout(binding=23, std430) readonly buffer MotionNormals { vec4 motionNormals[]; };
layout(binding=26, std430) readonly buffer Specular { vec4 specular[]; };
layout(binding=28, std430) readonly buffer SpecHistory { vec4 specHistory[]; };
layout(binding=29, std430) writeonly buffer SpecResolved { vec4 specResolved[]; };
layout(binding=32, std430) readonly buffer ShadingGuides { vec4 shadingGuides[]; };
layout(binding=33, std430) readonly buffer OldShadingGuides { vec4 oldShadingGuides[]; };
struct ReflectionGuide { vec4 position; vec4 normal; };
layout(binding=36, std430) readonly buffer ReflectionGuides { ReflectionGuide reflectionGuides[]; };
layout(binding=37, std430) readonly buffer OldReflectionGuides { ReflectionGuide oldReflectionGuides[]; };
layout(binding=38, std430) readonly buffer ReflectionMotion { ReflectionGuide reflectionMotion[]; };
layout(binding=39, std430) readonly buffer OldMoments { vec4 oldMoments[]; };
layout(binding=40, std430) writeonly buffer Moments { vec4 moments[]; };
layout(binding=41, std430) readonly buffer LightChange { vec4 lightChange[]; };
layout(binding=42, std430) readonly buffer Transmission { vec4 transmission[]; };
layout(binding=43, std430) readonly buffer TransmissionHistory { vec4 transmissionHistory[]; };
layout(binding=44, std430) writeonly buffer TransmissionResolved { vec4 transmissionResolved[]; };
layout(binding=45, std430) readonly buffer TransmissionGuides { TransmissionGuide transmissionGuides[]; };
layout(binding=46, std430) readonly buffer OldTransmissionGuides { TransmissionGuide oldTransmissionGuides[]; };
struct TransmissionMotion { vec4 position; vec4 normal; vec4 projection; };
layout(binding=47, std430) readonly buffer TransmissionMotions { TransmissionMotion transmissionMotions[]; };
layout(push_constant) uniform Constants {
    vec4 originNear, forwardFar, rightProjection, upProjection;
    vec4 sunExposure, parameters, sunRadiance, sampling;
} pc;

bool compatible(uint index,vec3 world,vec4 normal,float footprint) {
    vec4 previousPosition=previousPositions[index], previousNormal=previousNormals[index];
    if(previousPosition.w<=0 || floatBitsToUint(previousNormal.w)!=floatBitsToUint(normal.w) || dot(previousNormal.xyz,normal.xyz)<0.95) return false;
    vec3 separation=previousPosition.xyz-world;
    // Bilinear taps may lie one pixel away on the same surface. Tight plane
    // and normal tests reject disocclusions; a lateral limit rejects remote
    // coplanar surfaces with the same material.
    return abs(dot(separation,normal.xyz))<=max(0.03,footprint*0.1) &&
        dot(separation,separation)<=footprint*footprint*4;
}
vec3 clipHistory(ivec2 pixel,ivec2 size,vec3 oldColor,vec3 world,vec4 normal,float footprint,int channel,out vec2 stats) {
    vec3 mean=vec3(0), moment=vec3(0);
    float count=0;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
        ivec2 p=clamp(pixel+ivec2(x,y),ivec2(0),size-1);
        uint i=uint(p.y*size.x+p.x);
        uint center=uint(pixel.y*size.x+pixel.x);
        if(channel==2) {
            // Do not clip a nested path against pixels looking through a
            // different set of panes/volumes, even when their final target agrees.
            if(any(notEqual(transmissionGuides[center].identity,transmissionGuides[i].identity))) continue;
        } else if(channel==1) {
            ReflectionGuide target=reflectionGuides[center];
            ReflectionGuide neighbor=reflectionGuides[i];
            if(target.position.w>0 && (neighbor.position.w!=target.position.w ||
                floatBitsToUint(neighbor.normal.w)!=floatBitsToUint(target.normal.w))) continue;
        }
        if(positions[i].w<=0 || floatBitsToUint(normals[i].w)!=floatBitsToUint(normal.w) || dot(normals[i].xyz,normal.xyz)<0.95 ||
            abs(dot(positions[i].xyz-world,normal.xyz))>max(0.03,footprint*0.1)) continue;
        vec3 color=channel==2 ? transmission[i].rgb:(channel==1 ? specular[i].rgb:accumulated[i].rgb);
        mean+=color;
        moment+=color*color;
        count+=1;
    }
    mean/=max(count,1);
    vec3 deviation=sqrt(max(moment/max(count,1)-mean*mean,vec3(0)));
    stats=vec2(dot(mean,vec3(0.2126,0.7152,0.0722)),dot(deviation,vec3(0.2126,0.7152,0.0722)));
    vec3 radius=3*deviation+max(mean*0.1,vec3(0.0001));
    return clamp(oldColor,max(mean-radius,vec3(0)),mean+radius);
}
bool projectPrevious(vec3 world,ivec2 size,out vec2 pixel,out float footprint) {
    vec3 offset=world-rp.previousOrigin.xyz;
    float z=dot(offset,rp.previousForward.xyz);
    if(z<=0.001) return false;
    vec2 ndc=vec2(dot(offset,rp.previousRight.xyz)*rp.previousRight.w,
        dot(offset,rp.previousUp.xyz)*rp.previousUp.w)/z-rp.jitterHistoryReset.xy;
    pixel=(ndc*0.5+0.5)*vec2(size)-0.5;
    footprint=max(0.02,length(offset)*2/(float(size.y)*abs(rp.previousUp.w)));
    return !any(isnan(pixel)) && !any(isinf(pixel)) &&
        !any(lessThan(pixel,vec2(-0.5))) && !any(greaterThanEqual(pixel,vec2(size)-0.5));
}
bool mirrorCompatible(uint i,ReflectionGuide expected,vec3 plane,vec4 primary,float footprint) {
    ReflectionGuide old=oldReflectionGuides[i];
    if(old.position.w!=1 || floatBitsToUint(old.normal.w)!=floatBitsToUint(expected.normal.w) ||
        dot(old.normal.xyz,expected.normal.xyz)<0.95) return false;
    // Both rays must hit this mirror plane AND the same reflected object.
    if(floatBitsToUint(previousNormals[i].w)!=floatBitsToUint(primary.w) ||
        dot(previousNormals[i].xyz,primary.xyz)<0.999 || previousPositions[i].w<=0 ||
        abs(dot(previousPositions[i].xyz-plane,primary.xyz))>max(0.03,footprint*0.1)) return false;
    vec3 delta=old.position.xyz-expected.position.xyz;
    return abs(dot(delta,expected.normal.xyz))<=max(0.03,footprint*0.1) && dot(delta,delta)<=4*footprint*footprint;
}
bool waterReflectionCompatible(uint i,ReflectionGuide expected,vec3 targetNormal,uint targetID,
    vec4 primary,float interfaceFootprint,float targetFootprint) {
    // The old pixel must see the actual solved point on this finite interface,
    // not merely some distant point on an infinite, coplanar water surface.
    if(!compatible(i,expected.normal.xyz,primary,interfaceFootprint)) return false;
    ReflectionGuide old=oldReflectionGuides[i];
    if(old.position.w!=2 || floatBitsToUint(old.normal.w)!=targetID || dot(old.normal.xyz,targetNormal)<0.95) return false;
    vec3 delta=old.position.xyz-expected.position.xyz;
    return abs(dot(delta,targetNormal))<=max(0.03,targetFootprint*0.1) && dot(delta,delta)<=4*targetFootprint*targetFootprint;
}
float adaptiveCount(float count,vec2 before,vec2 stats,vec2 lighting) {
    // Deterministic, visibility-tested light changes carry no Monte Carlo
    // variance. Radiance-only changes must exceed estimated noise first.
    float scale=max(max(stats.x,before.x),0.01/max(pc.sunExposure.w,0.001));
    float lightReaction=smoothstep(0.03,0.3,abs(lighting.x-lighting.y)/scale);
    float variance=max(before.y-before.x*before.x,0);
    float excess=max(abs(stats.x-before.x)-3*sqrt(variance+stats.y*stats.y),0);
    float signalReaction=count>=3 ? smoothstep(0.15,0.75,excess/scale):0;
    return mix(count,1,max(lightReaction,signalReaction));
}

void resolveTransmission(uint index,ivec2 pixel,ivec2 size,vec4 position,vec4 normal,vec4 previousNormal) {
    // Opaque/unsupported pixels do not load the extra correspondence record.
    TransmissionGuide currentGuide=transmissionGuides[index];
    if(currentGuide.identity.w==0u) return;
    TransmissionMotion expected=transmissionMotions[index];
    if(expected.position.w<=0 || expected.projection.w<=0) return;
    vec2 previousPixel;
    float interfaceFootprint;
    if(!projectPrevious(expected.projection.xyz,size,previousPixel,interfaceFootprint)) return;
    float targetFootprint=max(0.02,length(expected.position.xyz-rp.previousOrigin.xyz)*2/
        (float(size.y)*abs(rp.previousUp.w)));
    ivec2 base=ivec2(floor(previousPixel));
    vec2 f=fract(previousPixel);
    vec3 oldColor=vec3(0);
    float total=0,oldCount=0;
    for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
        ivec2 p=base+ivec2(x,y);
        if(any(lessThan(p,ivec2(0))) || any(greaterThanEqual(p,size))) continue;
        uint i=uint(p.y*size.x+p.x);
        // Match the actual old interface point, not merely its infinite plane.
        if(!compatible(i,expected.projection.xyz,previousNormal,interfaceFootprint)) continue;
        TransmissionGuide old=oldTransmissionGuides[i];
        if(any(notEqual(old.identity,currentGuide.identity)) ||
            old.identity.x!=floatBitsToUint(expected.normal.w) ||
            dot(unpackReflectionNormal(old.position.w),expected.normal.xyz)<0.95) continue;
        vec3 delta=old.position.xyz-expected.position.xyz;
        if(abs(dot(delta,expected.normal.xyz))>max(0.03,targetFootprint*0.1) ||
            dot(delta,delta)>4*targetFootprint*targetFootprint) continue;
        float weight=(x==0 ? 1-f.x:f.x)*(y==0 ? 1-f.y:f.y);
        oldColor+=transmissionHistory[i].rgb*weight;
        oldCount+=transmissionHistory[i].w*weight;
        total+=weight;
    }
    if(total<0.25) return;
    vec2 stats;
    oldColor=clipHistory(pixel,size,oldColor/total,position.xyz,normal,interfaceFootprint,2,stats);
    // Transmission has independent clipping/counts. A conservative four-frame
    // cap limits lag while higher-order lighting correspondence remains absent.
    float count=min(oldCount/total+1,min(rp.jitterHistoryReset.z,4));
    transmissionResolved[index]=vec4(mix(oldColor,transmission[index].rgb,1/max(count,1)),count);
}
void main() {
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy), size=imageSize(outputColor);
    if(any(greaterThanEqual(pixel,size))) return;
    uint index=uint(pixel.y*size.x+pixel.x);
    vec3 current=accumulated[index].rgb;
    vec4 position=positions[index], normal=normals[index];
    resolved[index]=vec4(current,1);
    specResolved[index]=vec4(specular[index].rgb,1);
    transmissionResolved[index]=vec4(transmission[index].rgb,1);
    float d=dot(current,vec3(0.2126,0.7152,0.0722)),s=dot(specular[index].rgb,vec3(0.2126,0.7152,0.0722));
    vec4 resultMoments=vec4(d,d*d,s,s*s);
    moments[index]=resultMoments;
    vec4 previousSurface=motionPositions[index], previousNormal=motionNormals[index];
    if(rp.jitterHistoryReset.w!=0 || position.w<=0 || previousSurface.w<=0) return;
    resolveTransmission(index,pixel,size,position,normal,previousNormal);
    vec2 previousPixel;
    float footprint;
    bool projected=projectPrevious(previousSurface.xyz,size,previousPixel,footprint);
    float roughness=albedos[index].w;
    float diffuseLimit=rp.jitterHistoryReset.z;
#ifdef PT_SOFTWARE_HISTORY
    // Dynamic objects keep short history, but their motion no longer shortens
    // every static wall's history. Compatibility and local light-change tests
    // below still reject disocclusions and changed illumination. Moving shadows
    // rely on radiance anti-lag, not a previous-frame visibility trace.
    if((floatBitsToUint(normal.w)>>16)!=0u) diffuseLimit=min(diffuseLimit,4);
#endif
    // Diffuse history is view-independent; reflection history is not. Reject
    // it independently, including changed normal-map/wave normals and roughness.
    float viewAgreement=dot(normalize(position.xyz-pc.originNear.xyz),normalize(previousSurface.xyz-rp.previousOrigin.xyz));
    bool specCompatible=viewAgreement>=mix(0.9998,0.96,roughness*roughness);
    vec3 oldColor=vec3(0);
    float oldCount=0, total=0;
    vec2 dMoments=vec2(0);
    ivec2 base=ivec2(floor(projected ? previousPixel:vec2(0)));
    vec2 f=projected ? fract(previousPixel):vec2(0);
    if(projected) for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
        ivec2 p=base+ivec2(x,y);
        if(any(lessThan(p,ivec2(0))) || any(greaterThanEqual(p,size))) continue;
        uint i=uint(p.y*size.x+p.x);
        if(!compatible(i,previousSurface.xyz,previousNormal,footprint)) continue;
        float weight=(x==0 ? 1-f.x:f.x)*(y==0 ? 1-f.y:f.y);
        oldColor+=history[i].rgb*weight;
        oldCount+=history[i].w*weight;
        total+=weight;
        dMoments+=oldMoments[i].xy*weight;
    }
    // Do not amplify one tiny surviving tap on a silhouette.
    if(total>=0.25) {
        vec2 stats;
        oldColor=clipHistory(pixel,size,oldColor/total,position.xyz,normal,footprint,0,stats);
        float count=adaptiveCount(min(oldCount/total+1,diffuseLimit),dMoments/total,stats,lightChange[index].xy);
        resolved[index]=vec4(mix(oldColor,current,1/max(count,1)),count);
        resultMoments.xy=mix(dMoments/total,resultMoments.xy,1/max(count,1));
    }
    // Planar mirrors project a virtual target. Wavy water projects the solved
    // previous INTERFACE point and independently validates the real old target.
    // Neither reflection projection is used for diffuse or transmission.
    ReflectionGuide reflected=reflectionGuides[index];
    bool mirror=reflected.position.w==1, water=reflected.position.w==2;
    ReflectionGuide expected=ReflectionGuide(vec4(0),vec4(0));
    vec3 targetNormal=vec3(0);
    float targetFootprint=0;
    if(mirror || water) {
        expected=reflectionMotion[index];
        projected=expected.position.w>0 &&
            projectPrevious(water ? expected.normal.xyz:expected.position.xyz,size,previousPixel,footprint);
        if(water && projected) {
            targetNormal=unpackReflectionNormal(expected.normal.w);
            // A reflected object can be beside/behind the camera; direct
            // camera-to-target distance is not its optical-path footprint.
            float pathDistance=length(expected.normal.xyz-rp.previousOrigin.xyz)+
                length(expected.position.xyz-expected.normal.xyz);
            targetFootprint=max(0.02,pathDistance*2/
                (float(size.y)*abs(rp.previousUp.w)));
        }
        specCompatible=true;
    }
    if(reflected.position.w<0) projected=false;
    base=ivec2(floor(projected ? previousPixel:vec2(0)));
    f=projected ? fract(previousPixel):vec2(0);
    vec3 oldSpec=vec3(0);
    vec2 sMoments=vec2(0);
    float oldSpecCount=0,specTotal=0;
    if(projected && specCompatible) for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
        ivec2 p=base+ivec2(x,y);
        if(any(lessThan(p,ivec2(0))) || any(greaterThanEqual(p,size))) continue;
        uint i=uint(p.y*size.x+p.x);
        if(water) {
            if(!waterReflectionCompatible(i,expected,targetNormal,floatBitsToUint(reflected.normal.w),
                previousNormal,footprint,targetFootprint)) continue;
        } else if(mirror ? !mirrorCompatible(i,expected,previousSurface.xyz,previousNormal,footprint) :
            !compatible(i,previousSurface.xyz,previousNormal,footprint)) continue;
        // Optical correspondence already matched the PREVIOUS plane/wave and
        // target. Current/old shading-normal equality would reject real motion.
        if((!mirror && !water && dot(shadingGuides[index].xyz,oldShadingGuides[i].xyz)<=mix(0.9995,0.95,roughness)) ||
            abs(shadingGuides[index].w-oldShadingGuides[i].w)>=0.05) continue;
        float weight=(x==0 ? 1-f.x:f.x)*(y==0 ? 1-f.y:f.y);
        oldSpec+=specHistory[i].rgb*weight;
        oldSpecCount+=specHistory[i].w*weight;
        sMoments+=oldMoments[i].zw*weight;
        specTotal+=weight;
    }
    if(specTotal>=0.25) {
        vec2 stats;
        oldSpec=clipHistory(pixel,size,oldSpec/specTotal,position.xyz,normal,footprint,1,stats);
        float specLimit=min(rp.jitterHistoryReset.z,water ? 4:(mirror ? 8:mix(2,8,roughness*roughness)));
        float specCount=adaptiveCount(min(oldSpecCount/specTotal+1,specLimit),sMoments/specTotal,stats,lightChange[index].zw);
        specResolved[index]=vec4(mix(oldSpec,specular[index].rgb,1/max(specCount,1)),specCount);
        resultMoments.zw=mix(sMoments/specTotal,resultMoments.zw,1/max(specCount,1));
    }
    moments[index]=resultMoments;
}
