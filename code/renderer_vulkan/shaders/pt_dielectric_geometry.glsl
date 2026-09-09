// Geometry shared by the radiance path and its reconstruction guide. Keep
// wave time and the legacy parallel-pane lateral displacement identical.
vec3 waterShadingNormal(vec3 outward,vec3 world,float time) {
    vec3 slope=vec3(0.06*cos(world.x*0.07+time*1.3),0.045*cos(world.y*0.09-time*1.1),0);
    return normalize(outward-slope+outward*dot(outward,slope));
}
vec3 paneExit(vec3 world,vec3 incident,vec3 transmitted,vec3 geometric,float thickness,float bias) {
    vec3 offset=transmitted*(thickness/max(abs(dot(transmitted,geometric)),0.001));
    offset-=incident*(dot(offset,geometric)/min(dot(incident,geometric),-0.001));
    return world+offset-geometric*bias;
}

vec3 interfaceNormal(vec3 point,vec3 incident,vec3 facing,bool water,bool entering,float time) {
    vec3 n=water ? waterShadingNormal(entering ? facing:-facing,point,time):facing;
    // Match shadingNormal's geometric-hemisphere orientation BEFORE its
    // grazing-view fallback, including views from beneath the water surface.
    if(dot(n,facing)<0) n=-n;
    if(dot(n,-incident)<0.001) n=facing;
    return n;
}
bool interfaceReflection(vec3 point,vec3 camera,vec3 facing,bool water,bool entering,
    float time,float bias,out vec3 start,out vec3 outgoing) {
    vec3 incident=normalize(point-camera);
    if(dot(incident,facing)>=-0.001) return false;
    outgoing=reflect(incident,interfaceNormal(point,incident,facing,water,entering,time));
    if(dot(outgoing,facing)<=0.001) return false;
    start=point+facing*bias;
    return true;
}
bool interfaceTotalInternalReflection(vec3 point,vec3 camera,vec3 facing,vec2 optics,
    bool water,bool entering,float time,float bias,out vec3 start,out vec3 outgoing) {
    if(optics.y<0) return false; // A thin shell never changes the surrounding medium.
    vec3 incident=normalize(point-camera);
    vec3 n=interfaceNormal(point,incident,facing,water,entering,time);
    vec3 transmitted=refract(incident,n,optics.x);
    // Replaying a previously recorded TIR branch is valid only while that
    // branch still exists. A wave/plane change crossing the critical angle
    // must reject history, not reflect a ray which should have transmitted.
    if(dot(transmitted,transmitted)>=0.000001) return false;
    return interfaceReflection(point,camera,facing,water,entering,time,bias,start,outgoing);
}

// One refractive interface, or a parallel legacy pane. Normal-mapped interfaces
// and additional glass/water boundaries require a longer correspondence path.
bool interfaceTransmission(vec3 point,vec3 camera,vec3 facing,vec2 optics,
    bool water,bool entering,float time,float bias,out vec3 start,out vec3 outgoing) {
    vec3 incident=normalize(point-camera);
    if(dot(incident,facing)>=-0.001) return false;
    if(optics.y<0) {
        outgoing=incident;
        start=point+incident*bias;
        return true;
    }
    vec3 n=interfaceNormal(point,incident,facing,water,entering,time);
    vec3 transmitted=refract(incident,n,optics.x);
    if(dot(transmitted,transmitted)<0.000001 || dot(transmitted,facing)>=-0.001) return false;
    outgoing=optics.y>0 ? incident:normalize(transmitted);
    start=optics.y>0 ? paneExit(point,incident,transmitted,facing,optics.y,bias):point-facing*bias;
    return true;
}
