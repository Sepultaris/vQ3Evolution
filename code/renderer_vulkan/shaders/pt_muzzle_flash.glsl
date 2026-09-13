// Explicit cgame tags; do not guess from light radius, position or shader names.
// Uniform controls only; no added per-ray state or vertex attribute changes.
float weaponEmissionScale(uint primitive,bool visible) {
    uint flags=triangleMaterials[primitive];
    if((flags&0x07800000u)==0u) return 1;
    float lightScale=1;
    if(!visible) {
        if((flags&0x01000000u)!=0u) lightScale*=rocketEmission.z;
        if((flags&0x00800000u)!=0u) lightScale*=rocketEmission.w;
    }
    if((flags&0x04000000u)!=0u) {
        vec2 scales=uintBitsToFloat(emitterSearchControl.zw);
        return visible ? scales.x:scales.y*lightScale;
    }
    if((flags&0x02000000u)!=0u) return visible ? rocketEmission.x:rocketEmission.y*lightScale;
    return lightScale;
}
float emissionPower(uint primitive) {
    return materials[triangleMaterials[primitive]&0xffffu].emission.y*weaponEmissionScale(primitive,false);
}
