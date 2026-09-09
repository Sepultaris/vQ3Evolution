// RR consumes the SUM of lit radiance, not the native denoiser's four lobes.
// Keep the first-event rule (including its nonnegative specular clamp) and
// roulette throughput, but carry only one RGB weight and one RGB result.
vec3 rrPathWeight,rrRadiance;
uint pathClass;
void resetCompactPath() {
    rrPathWeight=vec3(1); rrRadiance=vec3(0); pathClass=0u;
}
void addIncident(vec3 light) { rrRadiance+=rrPathWeight*light; }
void addDirect(vec3 brdf,vec3 light) {
    if(pathClass==0u) brdf=diffuseBRDF+max(brdf-diffuseBRDF,vec3(0));
    rrRadiance+=(rrPathWeight*brdf)*light;
}
void attenuate(vec3 value) { rrPathWeight*=value; }
void compactReflect() { if(pathClass==0u) pathClass=1u; }
void compactTransmit() { if(pathClass==0u) pathClass=2u; }
void compactScatter(vec3 brdf,float factor) {
    if(pathClass==0u) {
        brdf=diffuseBRDF+max(brdf-diffuseBRDF,vec3(0));
        pathClass=1u;
    }
    rrPathWeight=rrPathWeight*brdf*factor;
}
vec3 compactThroughput() { return pathClass!=0u ? rrPathWeight:vec3(0); }
