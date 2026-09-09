// Query alpha remains the body's occlusion mask. Only radiance/guide hits
// reinterpret a failed body test as a transparent additive effect, so the
// existing straight-ray continuation preserves all geometry behind the glow.
Material surfaceHitMaterial(uint primitive,vec2 bary,vec3 observer) {
    Material m=materialProperties(triangleMaterials[primitive]&0xffffu);
    if(m.maps.w==3 && !passesAlpha(primitive,bary,observer)) m.params.x=2;
    return m;
}
