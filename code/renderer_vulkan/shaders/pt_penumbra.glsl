// Analytic source extent, not a post-process shadow blur. Zero consumes no RNG
// and retains the original delta-light directions, intensity and query length.
// Uniform solid-angle cone sampling: pbr-book.org/4ed/Shapes/Spheres.
vec3 penumbraCone(vec3 axis,float oneMinusCosine) {
    float offset=randomFloat()*oneMinusCosine;
    float sine=sqrt(max(offset*(2-offset),0));
    float phi=2*PI*randomFloat();
    return basisSample(axis,vec3(sine*cos(phi),sine*sin(phi),1-offset));
}
vec3 sampleSunPenumbra(vec3 axis) {
    if(skyEnvironment.z<=0) return axis;
    // 2*sin^2(theta/2) avoids cancellation for very small angular diameters.
    float sine=sin(0.5*skyEnvironment.z);
    return penumbraCone(axis,2*sine*sine);
}
float samplePointPenumbra(inout vec3 direction,float distance,out float shadowDistance) {
    shadowDistance=distance;
    float radius=skyEnvironment.w;
    if(radius<=0 || distance<=0.04) return 1;
    vec3 axis=direction;
    float cosine;
    float scale;
    if(distance>radius) {
        float ratio=radius/distance;
        float sinSquared=ratio*ratio;
        cosine=sqrt(max(1-sinSquared,0));
        direction=penumbraCone(axis,sinSquared/(1+cosine));
        // L*solidAngle / (I/d^2), with L=I/(pi*r^2). Preserves source
        // power instead of brightening a larger light by its surface area.
        scale=2/(1+cosine);
    } else {
        // Finite, two-sided analytic source when the shading point is inside.
        // The full sphere of incident directions replaces the exterior cone.
        direction=penumbraCone(axis,2);
        scale=4*max(distance*distance,1)/max(radius*radius,0.000001);
    }
    float projected=distance*dot(axis,direction);
    float discriminant=max(radius*radius-distance*distance+projected*projected,0);
    float root=sqrt(discriminant);
    // Stable near intersection outside; positive far intersection inside.
    shadowDistance=distance>radius ?
        (distance-radius)*(distance+radius)/max(projected+root,0.000001):projected+root;
    shadowDistance=max(shadowDistance,0.021);
    return scale;
}
