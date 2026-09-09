// Four per-hit values shared by emissive/sun/point lights and continuation.
// Do not regroup the color/energy formulas: retain the original arithmetic.
#ifdef PT_BRDF_REUSE
vec4 prepareBRDFView(vec3 n,vec3 v,vec3 albedo,float roughness,float metallic) {
    float nv=max(dot(n,v),0);
    float a=max(roughness*roughness,0.001), a2=a*a;
    return vec4(nv,a2,specularProbability(albedo,metallic),smith(nv,a2));
}
vec3 evaluateCachedBRDF(vec3 n,vec3 v,vec3 l,vec3 albedo,float metallic,vec4 view,out float pdf) {
    float nv=view.x, nl=max(dot(n,l),0);
    pdf=0;
    diffuseBRDF=vec3(0);
    if(nv<=0 || nl<=0) return vec3(0);
    vec3 h=normalize(v+l);
    float nh=max(dot(n,h),0), vh=max(dot(v,h),0.000001);
    float a2=view.y;
    float d=distribution(nh,a2);
    vec3 f=fresnel(mix(vec3(0.04),albedo,metallic),vh);
    float p=view.z;
    pdf=(1-p)*nl/PI+p*d*nh/(4*vh);
    diffuseBRDF=(1-f)*(1-metallic)*albedo/PI;
    return diffuseBRDF+f*d*view.w*smith(nl,a2)/max(4*nv*nl,0.000001);
}
vec3 sampleCachedBRDF(vec3 n,vec3 v,vec4 view) {
    float phi=2*PI*randomFloat(), xi=randomFloat();
    if(randomFloat()<view.z) {
        float cosine=sqrt((1-xi)/max(1+(view.y-1)*xi,0.000001));
        float sine=sqrt(max(0,1-cosine*cosine));
        vec3 h=basisSample(n,vec3(sine*cos(phi),sine*sin(phi),cosine));
        return reflect(-v,h);
    }
    return basisSample(n,vec3(sqrt(xi)*cos(phi),sqrt(xi)*sin(phi),sqrt(1-xi)));
}
// brdfView is invocation-local to the current opaque scattering hit, never
// shared across bounces/pixels or borrowed from reconstruction history.
#define prepareHitBRDF(n,v,albedo,roughness,metallic) vec4 brdfView=prepareBRDFView(n,v,albedo,roughness,metallic)
#define evaluateHitBRDF(n,v,l,albedo,roughness,metallic,pdf) evaluateCachedBRDF(n,v,l,albedo,metallic,brdfView,pdf)
#define sampleHitBRDF(n,v,albedo,roughness,metallic) sampleCachedBRDF(n,v,brdfView)
#else
#define prepareHitBRDF(n,v,albedo,roughness,metallic)
#define evaluateHitBRDF(n,v,l,albedo,roughness,metallic,pdf) evaluateBRDF(n,v,l,albedo,roughness,metallic,pdf)
#define sampleHitBRDF(n,v,albedo,roughness,metallic) sampleBRDF(n,v,albedo,roughness,metallic)
#endif
