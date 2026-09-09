// Project a ray cone's cross-section onto a triangle's plane, then express
// the two footprint axes in barycentrics. Keeping both axes lets the Vulkan
// sampler choose anisotropic taps instead of blurring both axes by the larger
// isotropic LOD. Degenerate triangles must never produce NaN derivatives.
mat2 surfaceBarycentricGradients(vec3 e1,vec3 e2,vec3 ray,float width) {
    vec3 areaNormal=cross(e1,e2);
    float area=length(areaNormal);
    if(area<0.000001 || width<=0) return mat2(0);
    vec3 normal=areaNormal/area;
    vec3 x=normalize(cross(abs(ray.z)<0.999 ? vec3(0,0,1):vec3(0,1,0),ray));
    vec3 y=cross(ray,x);
    float cosine=dot(normal,ray);
    cosine=(cosine<0 ? -1:1)*max(abs(cosine),0.001);
    vec3 dx=width*(x-ray*(dot(x,normal)/cosine));
    vec3 dy=width*(y-ray*(dot(y,normal)/cosine));
    vec3 dual1=cross(e2,normal)/area,dual2=cross(normal,e1)/area;
    return mat2(vec2(dot(dx,dual1),dot(dx,dual2)),vec2(dot(dy,dual1),dot(dy,dual2)));
}

uint footprintPrimitive=0xffffffffu;
mat2 footprintBarycentrics=mat2(0);
void setTextureFootprint(uint primitive,vec3 direction,float width) {
    uvec3 t=triangle(primitive);
    footprintPrimitive=primitive;
    footprintBarycentrics=surfaceBarycentricGradients(position(t.y)-position(t.x),
        position(t.z)-position(t.x),direction,width);
}
mat2 textureBarycentrics(uint primitive) {
    return primitive==footprintPrimitive ? footprintBarycentrics:mat2(0);
}
mat2 textureGradients(uint primitive,mat2 barycentrics) {
    uvec3 t=triangle(primitive);
    return mat2(vertices[t.y].uv.xy-vertices[t.x].uv.xy,
        vertices[t.z].uv.xy-vertices[t.x].uv.xy)*barycentrics;
}
float pixelConeSpread() {
    vec2 size=vec2(imageSize(outputColor));
    return max(2/(size.x*abs(pc.rightProjection.w)),2/(size.y*abs(pc.upProjection.w)));
}
vec4 sampleTexture(float textureIndex,vec2 uv,mat2 gradients) {
    return textureGrad(textures[nonuniformEXT(uint(textureIndex))],uv,gradients[0],gradients[1]);
}
vec4 triangleTexture(float textureIndex,uint primitive,vec2 bary) {
    return sampleTexture(textureIndex,triangleUV(primitive,bary),
        textureGradients(primitive,textureBarycentrics(primitive)));
}
