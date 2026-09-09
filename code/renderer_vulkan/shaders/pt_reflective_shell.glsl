float dielectricFresnel(float cosine,float etaI,float etaT) {
    cosine=clamp(abs(cosine),0,1);
    float sinT2=(etaI*etaI/(etaT*etaT))*max(0,1-cosine*cosine);
    if(sinT2>=1) return 1;
    float ct=sqrt(1-sinT2);
    float rs=(etaI*cosine-etaT*ct)/max(etaI*cosine+etaT*ct,0.000001);
    float rp=(etaT*cosine-etaI*ct)/max(etaT*cosine+etaI*ct,0.000001);
    return clamp((rs*rs+rp*rp)*0.5,0,1);
}
// Incoherent, zero-thickness dielectric sheet, including both interfaces.
// Pigment/coverage modifies reflectance; the complementary energy transmits.
void reflectiveShellWeights(float fresnel,vec4 artwork,out vec3 reflection,out vec3 transmission) {
    float sheet=2*fresnel/(1+fresnel);
    reflection=clamp(linearColor(artwork.rgb)*clamp(artwork.a,0,1)*sheet,0,1);
    transmission=1-reflection;
}
vec4 reflectiveShellAppearance(uint primitive,vec2 bary,vec3 observer) {
    uint id=triangleMaterials[primitive]&0xffffu;
    vec4 color=vec4(0);
    for(int layer=0;layer<int(materials[id].composition.x);++layer) {
        vec4 source=layerSample(primitive,bary,layer,mat2(0),observer,true);
        vec4 generators=materials[id].layers[layer].generators;
        if(!materialLayerAccepted(source,int(generators.z))) continue;
        vec4 sf,df;
        materialBlendFactors(source,color,uint(generators.w),sf,df);
        color=clamp(source*sf+color*df,0,1);
    }
    return color;
}
