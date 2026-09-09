// Immutable material records replace copied per-ray optical properties. The
// sentinel retains the exact camera-medium values supplied by the engine.
float mediumRecordIOR(uint id) {
    return id==0xffffffffu ? rp.cameraMedium.x:materials[id].optical.x;
}
vec3 mediumRecordAbsorption(uint id) {
    return id==0xffffffffu ? rp.cameraMedium.yzw:materials[id].absorption.rgb;
}
