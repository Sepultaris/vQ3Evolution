// Preserve the authored tcGen environment color/effect projection, with the
// observer belonging to THIS ray (also for reflections and light sampling).
// This image is material artwork, not a replacement for traced scene radiance.
vec2 environmentMaterialUV(vec3 normal,vec3 viewer,mat3 worldToLocal) {
    float n2=dot(normal,normal),v2=dot(viewer,viewer);
    if(n2<0.00000001 || v2<0.00000001) return vec2(0.5);
    vec3 n=normal*inversesqrt(n2),v=viewer*inversesqrt(v2);
    vec3 reflected=worldToLocal*(2*n*dot(n,v)-v);
    return vec2(0.5+reflected.y*0.5,0.5-reflected.z*0.5);
}
mat3 environmentMaterialBasis(vec3 worldE1,vec3 worldE2,vec3 localE1,vec3 localE2) {
    vec3 wn=cross(worldE1,worldE2),ln=cross(localE1,localE2);
    if(min(dot(wn,wn),dot(ln,ln))<0.00000001) return mat3(1);
    wn=normalize(wn); ln=normalize(ln);
    vec3 wx=normalize(worldE1),lx=normalize(localE1);
    // Recover the rigid model's original axes from existing position data;
    // no per-pixel buffer or extra per-vertex transform is required.
    return mat3(lx,cross(ln,lx),ln)*transpose(mat3(wx,cross(wn,wx),wn));
}
