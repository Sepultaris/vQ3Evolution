// Specular directional-hemispherical reflectance approximation from NVIDIA's
// Streamline DLSS-RR guide, section 4.2.1 (Ray Tracing Gems, chapter 32).
// Expanded scalar form keeps the HLSL row-major polynomial unambiguous in GLSL.
vec3 rrSpecularAlbedo(vec3 f0,float roughness,float NoV) {
    float n=clamp(abs(NoV),0,1), n3=n*n*n, a=roughness*roughness, a2=a*a;
    float bias=((0.99044-1.28514*n)+(1.29678-0.755907*n)*a)/
        ((1+2.92338*n+59.4188*n3)+(20.3225-27.0302*n+222.592*n3)*a+
         (121.563+626.13*n+316.627*n3)*a2);
    float scale=((0.0365463+3.32707*n)+(9.0632-9.04756*n)*a)/
        ((1+3.59685*n-1.36772*n3)+(9.04401-16.3174*n+9.22949*n3)*a+
         (5.56589+19.7886*n-20.2123*n3)*a2);
    bias*=clamp(f0.g*50,0,1);
    return clamp(f0*max(scale,0)+max(bias,0),0,1);
}
