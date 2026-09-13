// Legacy portal artwork is an unlit display coating, not a physical lamp.
// Invert the renderer's filmic curve so its authored display value survives
// tone mapping once, without being boosted again by scene exposure. This is
// applied only to the coating, never to the remote scene or its transmission.
float portalArtworkChannel(float display) {
    float y=pow(clamp(display,0,1),2.2);
    float a=2.51-2.43*y, b=0.03-0.59*y;
    float root=sqrt(b*b+0.56*a*y);
    // Rationalized root avoids cancellation in dark artwork (including black).
    return b>=0 ? (0.28*y)/(root+b) : (root-b)/(2*a);
}
vec3 portalArtworkRadiance(vec3 display) {
    return vec3(portalArtworkChannel(display.r),portalArtworkChannel(display.g),
        portalArtworkChannel(display.b))/max(pc.sunExposure.w,0.01);
}

// Keep the original ordered, premultiplied-alpha composition in texture space.
// Independently retain the existing HDR transmission factors for scene rays.
void portalCoating(uint primitive,vec2 bary,vec3 observer,out vec3 coating,out vec3 transmission) {
    uint id=triangleMaterials[primitive]&0xffffu;
    coating=vec3(0); transmission=vec3(1);
    for(int layer=0;layer<int(materials[id].composition.x);++layer) {
        vec4 source=layerSample(primitive,bary,layer,textureBarycentrics(primitive),observer);
        if(!materialLayerAccepted(source,int(materials[id].layers[layer].generators.z))) continue;
        vec4 sf0,df0,sf1,df1;
        uint blend=uint(materials[id].layers[layer].generators.w);
        materialBlendFactors(source,vec4(0,0,0,1),blend,sf0,df0);
        materialBlendFactors(source,vec4(1),blend,sf1,df1);
        vec3 bias=source.rgb*sf0.rgb;
        vec3 gain=source.rgb*(sf1.rgb-sf0.rgb)+df0.rgb;
        coating=clamp(bias+gain*coating,0,1);
        source.rgb=linearColor(source.rgb);
        materialBlendFactors(source,vec4(0,0,0,1),blend,sf0,df0);
        materialBlendFactors(source,vec4(1),blend,sf1,df1);
        transmission*=source.rgb*(sf1.rgb-sf0.rgb)+df0.rgb;
    }
    coating=portalArtworkRadiance(coating);
}

bool portalRay(uint primitive,vec3 world,inout vec3 origin,inout vec3 direction) {
    uint slot=portalIndex(primitive);
    if(slot==0u) return false;
    slot--;
    vec4 plane=portals[slot].clip;
    if(dot(plane.xyz,plane.xyz)<0.5) return false; // Missing entity / r_noportals.
    vec4 point=vec4(world,1);
    origin=vec3(dot(portals[slot].rows[0],point),dot(portals[slot].rows[1],point),dot(portals[slot].rows[2],point));
    direction=normalize(vec3(dot(portals[slot].rows[0].xyz,direction),
        dot(portals[slot].rows[1].xyz,direction),dot(portals[slot].rows[2].xyz,direction)));
    float forward=dot(plane.xyz,direction);
    if(forward<=0.00001) return false;
    // The aperture may wave across the undeformed plane. Clip the destination
    // to the camera plane, as the raster portal does, rather than hitting its back wall.
    float offset=max((plane.w-dot(plane.xyz,origin))/forward,0);
    origin+=direction*(offset+pc.parameters.x);
    return true;
}
