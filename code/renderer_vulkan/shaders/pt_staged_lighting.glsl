// This continuation is checked against the original integrator.
uint stagedLighting() {
    vec3 v=-direction;
    prepareHitBRDF(n,v,albedo,roughness,metallic);
#include "pt_light_loop.glsl"
        // Direct illumination/emission above still use the final interaction.
        // Avoid sampling/evaluating a continuation that cannot be traced.
        PT_CATEGORY(6u);
        if(bounce+1>=int(pc.sampling.y)) return 0u;
        vec3 next=sampleHitBRDF(n,v,albedo,roughness,metallic);
        if(dot(next,geometric)<=0) return 0u;
        float pdf;
        vec3 brdf=evaluateHitBRDF(n,v,next,albedo,roughness,metallic,pdf);
        if(pdf<=0.0000001) return 0u;
        float factor=max(dot(n,next),0)/pdf;
#ifdef PT_COMPACT_TRANSPORT
        compactScatter(brdf,factor);
#else
        pathD=(pathD*brdf+pathE*diffuseBRDF)*factor;
        pathS=(pathS*brdf+pathE*max(brdf-diffuseBRDF,vec3(0)))*factor;
        pathT*=brdf*factor;
        pathE=vec3(0);
#endif
        previousPDF=pdf;
        if(bounce>=2) {
#ifdef PT_COMPACT_TRANSPORT
            vec3 throughput=compactThroughput();
#else
            vec3 throughput=pathD+pathS+pathT;
#endif
            float survival=clamp(max(throughput.r,max(throughput.g,throughput.b)),0.05,0.95);
            if(randomFloat()>survival) return 0u;
            attenuate(vec3(1/survival));
        }
        origin=start; direction=next;
        segmentDistance=0;
    return 1u;
}
