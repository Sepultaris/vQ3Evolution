// Native receiver-material decals. Coplanar marks are not displaced geometry,
// floating occluders, or a post-process overlay; all shaded ray hits see them.
const float DECAL_PLANE_EPSILON=0.05;
const int MAX_HIT_DECALS=16;
bool baseColorHasDecal=false;
#ifdef PT_COMPACT_DECALS
#define DECAL_POLYGON(index) uint(vertices[triangle(primitives[index]).x].local.w)
#else
#define DECAL_POLYGON(index) polygons[index]
#endif

bool decalReceiver(uint receiver,vec3 world) {
    return decalMins.w>0 && (triangleMaterials[receiver]&0x10000000u)==0u &&
        all(greaterThanEqual(world,decalMins.xyz-DECAL_PLANE_EPSILON)) &&
        all(lessThanEqual(world,decalMaxs.xyz+DECAL_PLANE_EPSILON));
}
bool decalCandidate(uint primitive,vec3 normal) {
    if((triangleMaterials[primitive]&0x08000000u)==0u) return false;
    float area;
    return abs(dot(geometricNormal(primitive,area),normal))>0.99 && area>0.000001;
}
bool receiverHasDecal(uint receiver,vec3 world) {
    if(!decalReceiver(receiver,world)) return false;
    float area;
    vec3 normal=geometricNormal(receiver,area);
    rayQueryEXT query;
    rayQueryInitializeEXT(query,sceneAS,gl_RayFlagsNoneEXT,16u,
        world+normal*DECAL_PLANE_EPSILON,0.001,-normal,2*DECAL_PLANE_EPSILON);
    while(rayQueryProceedEXT(query)) {
        if(rayQueryGetIntersectionTypeEXT(query,false)==gl_RayQueryCandidateIntersectionTriangleEXT &&
            decalCandidate(queryPrimitive(query,false),normal)) return true;
    }
    return false;
}
vec3 applyDecals(uint receiver,vec2 receiverBary,vec3 albedo,vec3 observer,out bool touched) {
    touched=false;
    uvec3 r=triangle(receiver);
    vec3 world=position(r.x)*(1-receiverBary.x-receiverBary.y)+position(r.y)*receiverBary.x+position(r.z)*receiverBary.y;
    if(!decalReceiver(receiver,world)) return albedo;
    float area;
    vec3 normal=geometricNormal(receiver,area);
    uint primitives[MAX_HIT_DECALS];
#ifndef PT_COMPACT_DECALS
    uint polygons[MAX_HIT_DECALS];
#endif
    vec2 barycentrics[MAX_HIT_DECALS];
    int count=0;
    rayQueryEXT query;
    rayQueryInitializeEXT(query,sceneAS,gl_RayFlagsNoneEXT,16u,
        world+normal*DECAL_PLANE_EPSILON,0.001,-normal,2*DECAL_PLANE_EPSILON);
    while(rayQueryProceedEXT(query)) {
        if(rayQueryGetIntersectionTypeEXT(query,false)!=gl_RayQueryCandidateIntersectionTriangleEXT) continue;
        uint primitive=queryPrimitive(query,false);
        if(!decalCandidate(primitive,normal)) continue;
        uint polygon=uint(vertices[triangle(primitive).x].local.w);
        bool duplicate=false;
        for(int i=0;i<count;++i) if(DECAL_POLYGON(i)==polygon) duplicate=true;
        if(duplicate) continue; // A shared fan edge must not darken a mark twice.
        touched=true;
        // Preserve deterministic raster submission order, independent of BVH
        // candidate traversal order. Retain the last 16 layers if heavily stacked.
        int at=count;
        if(count==MAX_HIT_DECALS) {
            if(primitive<primitives[0]) continue;
            for(int i=1;i<count;++i) {
                primitives[i-1]=primitives[i];
#ifndef PT_COMPACT_DECALS
                polygons[i-1]=polygons[i];
#endif
                barycentrics[i-1]=barycentrics[i];
            }
            at=count-1;
        } else ++count;
        while(at>0 && primitives[at-1]>primitive) {
            primitives[at]=primitives[at-1];
#ifndef PT_COMPACT_DECALS
            polygons[at]=polygons[at-1];
#endif
            barycentrics[at]=barycentrics[at-1];
            --at;
        }
        primitives[at]=primitive;
#ifndef PT_COMPACT_DECALS
        polygons[at]=polygon;
#endif
        barycentrics[at]=rayQueryGetIntersectionBarycentricsEXT(query,false);
    }
    // Map the receiver's footprint into each coplanar mark's UV basis without
    // overwriting the caller's current texture footprint for other material maps.
    mat2 receiverGradients=textureBarycentrics(receiver);
    vec3 re1=position(r.y)-position(r.x),re2=position(r.z)-position(r.x);
    vec3 dx=re1*receiverGradients[0].x+re2*receiverGradients[0].y;
    vec3 dy=re1*receiverGradients[1].x+re2*receiverGradients[1].y;
    for(int i=0;i<count;++i) {
        uint primitive=primitives[i];
        uvec3 t=triangle(primitive);
        vec3 e1=position(t.y)-position(t.x),e2=position(t.z)-position(t.x);
        vec3 n=cross(e1,e2);
        float a=max(length(n),0.000001); n/=a;
        vec3 dual1=cross(e2,n)/a,dual2=cross(n,e1)/a;
        mat2 gradients=mat2(vec2(dot(dx,dual1),dot(dx,dual2)),vec2(dot(dy,dual1),dot(dy,dual2)));
        vec4 color=materialColor(primitive,barycentrics[i],gradients,observer);
        Material material=materialProperties(triangleMaterials[primitive]&0xffffu);
        if((material.surface.w==1 && color.a<=0) || (material.surface.w==2 && color.a>=0.5) ||
            (material.surface.w==3 && color.a<0.5)) continue;
        vec3 linear=clamp(linearColor(color.rgb),0,1);
        if(material.params.x==3) albedo*=material.absorption.w!=0 ? 1-linear:linear;
        else albedo=mix(albedo,linear,material.params.x==1 ? clamp(color.a,0,1):1);
    }
    return albedo;
}
vec3 baseColor(uint primitive,vec2 bary,vec3 observer) {
    Material m=materialProperties(triangleMaterials[primitive]&0xffffu);
    vec3 color=linearColor(m.maps.x>=0 ? triangleTexture(m.maps.x,primitive,bary).rgb:materialColor(primitive,bary,observer).rgb);
    return clamp(applyDecals(primitive,bary,color,observer,baseColorHasDecal),vec3(0),vec3(0.98));
}
