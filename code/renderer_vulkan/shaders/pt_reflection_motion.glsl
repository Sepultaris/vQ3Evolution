// ReflectionGuide.position.w: 0 = ordinary/unsupported, 1 = planar virtual
// target, 2 = real water-reflected target, -1 = water with no reusable target.
// Water motion reuses the existing 32-byte ReflectionGuide allocation:
//   position.xyz = tracked previous REAL target, position.w = validity
//   normal.xyz = solved previous interface point, normal.w = packed normal
// Target identity comes from the current guide: hitCorrespondence uses the
// same material/object ID for current and previous vertices of that triangle.
// Planar virtual-target motion keeps its original position/normal/ID layout.
float packReflectionNormal(vec3 normal) {
    normal/=dot(abs(normal),vec3(1));
    vec2 oct=normal.xy;
    if(normal.z<0) oct=(1-abs(oct.yx))*mix(vec2(-1),vec2(1),greaterThanEqual(oct,vec2(0)));
    return uintBitsToFloat(packSnorm2x16(oct));
}
vec3 unpackReflectionNormal(float packed) {
    vec2 oct=unpackSnorm2x16(floatBitsToUint(packed));
    vec3 normal=vec3(oct,1-abs(oct.x)-abs(oct.y));
    if(normal.z<0) normal.xy=(1-abs(oct.yx))*mix(vec2(-1),vec2(1),greaterThanEqual(oct,vec2(0)));
    return normalize(normal);
}
