// The first non-transparent event selects either emission-only, D/S mixture,
// or transmission transport. These states are mutually exclusive. Keep the
// D/S split exact, but stop carrying four independent RGB weights per path.
vec3 pathA,pathB;
uint pathClass;
void resetCompactPath() {
    pathA=vec3(1); pathB=vec3(0); pathClass=0u;
    radianceD=radianceS=radianceT=radianceE=vec3(0);
}
void addIncident(vec3 light) {
    if(pathClass==0u) radianceE+=pathA*light;
    else if(pathClass==1u) { radianceD+=pathA*light; radianceS+=pathB*light; }
    else radianceT+=pathA*light;
}
void addDirect(vec3 brdf,vec3 light) {
    if(pathClass==0u) {
        radianceD+=(pathA*diffuseBRDF)*light;
        radianceS+=(pathA*max(brdf-diffuseBRDF,vec3(0)))*light;
    } else if(pathClass==1u) {
        radianceD+=(pathA*brdf)*light;
        radianceS+=(pathB*brdf)*light;
    } else radianceT+=pathA*brdf*light;
}
void attenuate(vec3 value) { pathA*=value; pathB*=value; }
void compactReflect() {
    if(pathClass==0u) { pathB=pathA; pathA=vec3(0); pathClass=1u; }
}
void compactTransmit() { if(pathClass==0u) pathClass=2u; }
void compactScatter(vec3 brdf,float factor) {
    if(pathClass==0u) {
        pathB=(pathA*max(brdf-diffuseBRDF,vec3(0)))*factor;
        pathA=(pathA*diffuseBRDF)*factor;
        pathClass=1u;
    } else if(pathClass==1u) {
        pathA=pathA*brdf*factor;
        pathB=pathB*brdf*factor;
    } else pathA*=brdf*factor;
}
vec3 compactThroughput() { return pathClass==1u ? pathA+pathB:(pathClass==2u ? pathA:vec3(0)); }
