// Same triangle coordinates and intensity; only the memory indirections differ.
layout(constant_id=2) const bool ptEmitterGeometry=false;
void sampledEmitterGeometry(uint primitive,uint emitter,float root,vec2 bary,
    out vec3 target,out vec3 normal,out float power) {
    vec3 a,b,c;
    if(ptEmitterGeometry) {
        a=emitterGeometry[emitter].a.xyz;
        b=emitterGeometry[emitter].b.xyz;
        c=emitterGeometry[emitter].c.xyz;
        power=emitterGeometry[emitter].a.w;
    } else {
        uvec3 t=triangle(primitive);
        a=position(t.x); b=position(t.y); c=position(t.z);
        power=materials[triangleMaterials[primitive]&0xffffu].emission.y;
    }
    target=a*(1-root)+b*bary.x+c*bary.y;
    vec3 crossEdges=cross(b-a,c-a);
    normal=crossEdges/max(length(crossEdges),0.000001);
}
