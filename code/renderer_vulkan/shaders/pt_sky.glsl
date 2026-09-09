#include "pt_sky_projection.glsl"
vec3 skyRadiance(uint id,vec3 direction,float spread) {
    vec3 d=normalize(direction);
    if(materials[id].emission.z==2)
        return linearColor(sampleTexture(materials[id].surface.x,vec2(0),mat2(0)).rgb);
    int face=skyFace(d);
    vec2 uv=skyFaceUV(d,face);
    vec3 x=normalize(cross(abs(d.z)<0.999 ? vec3(0,0,1):vec3(0,1,0),d));
    vec3 y=cross(d,x);
    // Derivatives stay on this face even across an edge. A face switch would
    // turn a small footprint into a whole-image mip at cubemap boundaries.
    vec3 dx=normalize(d+x*spread),dy=normalize(d+y*spread);
    float textureIndex=materials[id].skybox[face/4][face%4];
    vec4 color=vec4(0,0,0,1);
    if(textureIndex>=0) {
        mat2 gradients=mat2(skyFaceUV(dx,face)-uv,skyFaceUV(dy,face)-uv);
        color=sampleTexture(textureIndex,uv,gradients);
    }
    float height=materials[id].skybox[1].z;
    int count=int(materials[id].skybox[1].w);
    // The original FillCloudBox builds five sides, never the downward face.
    if(count>0 && height>0 && face!=5) {
        uv=skyCloudUV(d,height);
        mat2 gradients=mat2(skyCloudUV(dx,height)-uv,skyCloudUV(dy,height)-uv);
        float distance=pc.forwardFar.w/1.75;
        vec3 cube=d/max(max(abs(d.x),abs(d.y)),abs(d.z));
        vec3 cubeX=dx/max(max(abs(dx.x),abs(dx.y)),abs(dx.z));
        vec3 cubeY=dy/max(max(abs(dy.x),abs(dy.y)),abs(dy.z));
        LayerContext context;
        context.local=pc.originNear.xyz+cube*distance;
        context.localDx=(cubeX-cube)*distance;
        context.localDy=(cubeY-cube)*distance;
        context.vertexColor=context.entityColor=vec4(1);
        context.timeScroll=vec3(0); context.world=true; context.uniformTint=false;
        for(int layer=0;layer<count;++layer) {
            vec4 source=layerSampleUV(id,layer,uv,gradients,context);
            vec4 generators=materials[id].layers[layer].generators;
            if(!materialLayerAccepted(source,int(generators.z))) continue;
            vec4 sf,df;
            materialBlendFactors(source,color,uint(generators.w),sf,df);
            color=clamp(source*sf+color*df,0,1);
        }
    }
    // The authored sky is radiance, including its additive clouds. Do not use
    // q3map_lightimage (a map compiler lighting proxy) as the visible skybox.
    return linearColor(color.rgb);
}
vec3 environment(vec3 direction,float spread) {
    if(skyEnvironment.x>0) return skyRadiance(uint(skyEnvironment.x-1),direction,spread);
    // Retain the existing fallback only for maps without an authored sky.
    return mix(vec3(0.015,0.02,0.03),vec3(0.12,0.18,0.3),clamp(direction.z,0,1));
}
