// Optional artistic, unoccluded diffuse fill. This does not scale lights,
// emissive textures, sky or the HUD, and is not a substitute for traced GI.
// Apply before tone mapping and through the existing path throughput so filled
// surfaces remain visible in reflections/transmission. A perfect metal has no
// diffuse fill of its own; it still reflects other illuminated surfaces.
void addAmbient(vec3 albedo,float metallic,float strength) {
    if(strength<=0) return; // Default off: no RNG, rays or radiance/state changes.
    diffuseBRDF=albedo*(0.96*(1-clamp(metallic,0,1)));
    addDirect(diffuseBRDF,vec3(strength));
}
