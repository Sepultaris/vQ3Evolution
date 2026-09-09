// Generalized RIS with strictly positive target support. A reused reservoir
// contributes p_hat_here(y) * W_there * M; the final estimator uses W = sum/M/p_hat.
// History count is capped BEFORE merging, scaling its weight by the same count.
// Visibility is never stored or reused. This is not radiance accumulation.
void rrStream(inout vec4 r,inout float sum,uint light,float weight,float count,float u) {
    r.z+=count;
    sum+=weight;
    if(weight>0 && u*sum<weight) r.x=float(light);
}
float rrNormalize(float sum,float count,float target) {
    return count>0 && target>0 ? sum/(count*target):0;
}
float rrRelativeVariance(vec4 moments) {
    return max(moments.y-moments.x*moments.x,0)/max(moments.x*moments.x,0.0001);
}
float rrSampleBudget(float maximum,vec4 moments,bool safe) {
    // Never early-stop based on the current samples: that biases the estimator.
    // Use prior-frame evidence only, with at least half the requested samples.
    if(!safe || moments.z<8 || rrRelativeVariance(moments)>0.15) return maximum;
    return max(1,ceil(maximum*0.5));
}
// Strict moving-history acceptance: a material match alone cannot distinguish
// nearby parallel walls, silhouettes, or a sharp material/roughness boundary.
bool rrAdaptiveSurfaceMatch(uint currentID,uint oldID,float normalAgreement,
    float separation,float planeDistance,float roughnessDelta,float footprint) {
    return currentID!=0u && currentID==oldID && normalAgreement>0.98 &&
        separation<2*footprint && planeDistance<max(0.02,footprint*0.15) && roughnessDelta<0.05;
}
bool rrMovingHistorySafe(float motion,float limit,vec4 moments) {
    // More evidence and lower variance than the stationary path. Fast turns
    // retain the full budget even if a single geometric match happens to pass.
    return motion<limit && moments.z>=12 && rrRelativeVariance(moments)<=0.08;
}
