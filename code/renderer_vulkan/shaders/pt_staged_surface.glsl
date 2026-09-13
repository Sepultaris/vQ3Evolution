// This body is checked against the original integrator by pt_staged_check.py.
// 0 = terminated, 1 = path continuation, 2 = opaque lighting ready.
uint stagedSurface() {
    for(;;) {
        PT_CATEGORY(0u);
        Hit hit;
        float fogDistance; uint fogVolume;
        bool found=fogPathHit(origin,direction,segmentDistance,previousPDF==-2 ? 1:bounce,hit,fogDistance,fogVolume);
        PT_CATEGORY(2u);
        if(!found && fogVolume!=0xffffffffu) {
            PT_COUNT(4u);
            float traveled=max(fogDistance-segmentDistance,0);
            coneWidth+=traveled*coneSpread;
            if(mediumCount>0) attenuate(exp(-MEDIUM_ABSORPTION(mediumCount-1)*traveled));
            addIncident(fogVolumes[fogVolume].emission.rgb);
            vec3 fogWeight;
            if(!fogScatter(fogVolume,fogWeight)) return 0u;
            attenuate(fogWeight);
            if(!any(greaterThan(fogVolumes[fogVolume].albedo.rgb,vec3(0)))) return 0u;
            // A first volume scatter is diffuse transport; later events retain
            // the already selected reflection/transmission contribution class.
#ifdef PT_COMPACT_TRANSPORT
            if(pathClass==0u) pathClass=1u;
#else
            pathD+=pathE; pathE=vec3(0);
#endif
            vec3 fogPoint=origin+direction*fogDistance;
            visibilityAbsorption=mediumCount>0 ? MEDIUM_ABSORPTION(mediumCount-1):vec3(0);
            addIncident(fogLighting(fogPoint,bounce+1>=int(pc.sampling.y)));
            // Isotropic ambient fill also reaches surviving volume scatters;
            // absorption/scattering albedo and path attenuation already apply.
            if(skyEnvironment.y>0) addIncident(vec3(skyEnvironment.y));
            if(bounce+1>=int(pc.sampling.y)) return 0u;
            origin=fogPoint; direction=fogDirection();
            coneSpread=max(coneSpread,1);
            previousPDF=1/(4*PI); segmentDistance=0;
            return 1u;
        }
        if(!found) {
            if(mediumCount>0) attenuate(exp(-MEDIUM_ABSORPTION(mediumCount-1)*max(pc.parameters.y-segmentDistance,0)));
            addIncident(environment(direction,coneSpread));
            return 0u;
        }
        Material m=surfaceHitMaterial(hit.primitive,hit.bary,origin);
        PT_COUNT(3u);
        float traveled=max(hit.distance-segmentDistance,0);
        coneWidth+=traveled*coneSpread;
        setTextureFootprint(hit.primitive,direction,coneWidth);
        if(mediumCount>0) attenuate(exp(-MEDIUM_ABSORPTION(mediumCount-1)*traveled));
        if(m.emission.z!=0) {
            // Some legacy skies (notably blacksky) have SURF_SKY but no skyparms.
            // They retain their source texture, while all sky boundaries pass sun rays.
            vec3 sky=m.emission.z==2 ? linearColor(materialColor(hit.primitive,hit.bary,origin).rgb) :
                skyRadiance(triangleMaterials[hit.primitive]&0xffffu,direction,coneSpread);
            addIncident(sky);
            return 0u;
        }
        vec3 world=origin+direction*hit.distance;
        if(portalIndex(hit.primitive)!=0u) {
            vec3 coating,transmission;
            portalCoating(hit.primitive,hit.bary,origin,coating,transmission);
            addIncident(coating);
            attenuate(transmission);
            if(++transparentLayers>=32 || !any(greaterThan(transmission,vec3(0.00001))) ||
                !portalRay(hit.primitive,world,origin,direction)) return 0u;
            // The destination has its own medium; never carry local glass absorption
            // through a camera teleport. Fog volumes are queried at the new origin.
            mediumCount=0; previousPDF=-2; segmentDistance=0;
            continue;
        }
        if(m.params.x==4) {
            // There is no next ray at the bounce limit. This interface emits
            // nothing, so sampling a discarded delta lobe cannot add radiance.
            if(bounce+1>=int(pc.sampling.y)) return 0u;
            vec3 n=shadingNormal(hit,direction,geometric);
            uvec3 t=triangle(hit.primitive);
            vec3 outward=vertices[t.x].normal.xyz*(1-hit.bary.x-hit.bary.y)+
                vertices[t.y].normal.xyz*hit.bary.x+vertices[t.z].normal.xyz*hit.bary.y;
            bool entering=dot(outward,direction)<0;
            float etaI=mediumCount>0 ? MEDIUM_IOR(mediumCount-1):1;
            float etaT=entering ? m.optical.x : (mediumCount>1 ? MEDIUM_IOR(mediumCount-2):1);
            // A back face as the first boundary means the camera started inside.
            if(!entering && mediumCount==0 && m.optical.y==0) {
                etaI=m.optical.x;
                attenuate(exp(-m.absorption.rgb*hit.distance));
#ifdef PT_COMPACT_MEDIA
                mediumRecords[0]=triangleMaterials[hit.primitive]&0xffffu; mediumCount=1;
#else
                mediumIOR[0]=etaI; mediumAbsorption[0]=m.absorption.rgb; mediumCount=1;
#endif
            }
            bool shell=m.optical.y<0,thin=m.optical.y!=0;
            if(thin) etaT=m.optical.x;
            float reflectance=dielectricFresnel(dot(-direction,n),etaI,etaT);
            vec3 transmitted=refract(direction,n,etaI/etaT);
            if(dot(transmitted,transmitted)<0.000001) reflectance=1;
            // A finite parallel pane includes both interfaces and the geometric
            // series of internal reflections. Transmitted rays leave parallel,
            // but with the real lateral offset from their path inside the slab.
            vec3 paneTransmission=vec3(1), paneReflection=vec3(reflectance);
            float paneProbability=reflectance;
            if(shell) {
                reflectiveShellWeights(reflectance,reflectiveShellAppearance(hit.primitive,hit.bary,origin),
                    paneReflection,paneTransmission);
                paneProbability=max(paneReflection.r,max(paneReflection.g,paneReflection.b));
            } else if(m.optical.y>0 && reflectance<1) {
                float distance=m.optical.y/max(abs(dot(transmitted,geometric)),0.001);
                vec3 absorption=m.absorption.rgb;
                if(m.maps.w>0) absorption-=log(max(baseColor(hit.primitive,hit.bary,origin),vec3(0.01)))/max(m.optical.y,1);
                vec3 a=exp(-absorption*distance);
                vec3 denominator=max(1-reflectance*reflectance*a*a,vec3(0.000001));
                paneTransmission=(1-reflectance)*(1-reflectance)*a/denominator;
                paneReflection=vec3(reflectance)+(1-reflectance)*(1-reflectance)*reflectance*a*a/denominator;
                paneProbability=clamp(max(paneReflection.r,max(paneReflection.g,paneReflection.b)),0.001,0.999);
            }
            if(randomFloat()<(thin ? paneProbability:reflectance)) {
#ifdef PT_COMPACT_TRANSPORT
                compactReflect();
#else
                pathS+=pathE; pathE=vec3(0);
#endif
                if(thin) attenuate(paneReflection/max(paneProbability,0.000001));
                direction=reflect(direction,n);
                if(dot(direction,geometric)<=0) return 0u;
                origin=world+geometric*pc.parameters.x;
            } else {
#ifdef PT_COMPACT_TRANSPORT
                compactTransmit();
#else
                pathT+=pathE; pathE=vec3(0);
#endif
                if(thin) {
                    attenuate(paneTransmission/max(1-paneProbability,0.000001));
                    // Equivalent zero-thickness exit on the surface plane avoids
                    // skipping real geometry behind a legacy single-polygon pane.
                    origin=shell ? world+direction*pc.parameters.x:
                        paneExit(world,direction,transmitted,geometric,m.optical.y,pc.parameters.x);
                } else {
                    if(dot(transmitted,geometric)>=0) return 0u;
                    attenuate(vec3((etaI*etaI)/(etaT*etaT)));
                    direction=normalize(transmitted);
                    coneSpread*=etaI/etaT;
                    origin=world-geometric*pc.parameters.x;
                    if(entering) {
                        if(mediumCount>=4) return 0u;
#ifdef PT_COMPACT_MEDIA
                        mediumRecords[mediumCount++]=triangleMaterials[hit.primitive]&0xffffu;
#else
                        mediumIOR[mediumCount]=etaT;
                        mediumAbsorption[mediumCount++]=m.absorption.rgb;
#endif
                    } else mediumCount=max(mediumCount-1,0);
                }
            }
            previousPDF=-1; segmentDistance=0;
            return 1u;
        }
        if(m.params.x==2 || m.params.x==3) {
            if(m.params.x==2) {
                float area;
                vec3 normal=geometricNormal(hit.primitive,area);
                float d=hit.distance;
                float lightPDF=emitterPDF(hit.primitive,d*d,abs(dot(normal,-direction)));
                addIncident(emissionAt(hit.primitive,hit.bary,true,origin,bounce==0 || previousPDF<0)*
                    (bounce==0 || previousPDF<0 ? 1:powerWeight(previousPDF,lightPDF)));
            } else attenuate(filterTransmission(hit.primitive,hit.bary,textureBarycentrics(hit.primitive),origin));
            if(++transparentLayers>=32) return 0u;
            segmentDistance=hit.distance;
            continue;
        }
        n=shadingNormal(hit,direction,geometric);
        albedo=baseColor(hit.primitive,hit.bary,origin);
        vec2 properties=surfaceProperties(hit.primitive,hit.bary);
        roughness=properties.x; metallic=properties.y;
        addAmbient(albedo,metallic,skyEnvironment.y);
        if(m.emission.y>0) {
            float area;
            vec3 normal=geometricNormal(hit.primitive,area);
            float d=hit.distance;
            float lightPDF=emitterPDF(hit.primitive,d*d,abs(dot(normal,-direction)));
            float weight=bounce==0 || previousPDF<0 ? 1 : powerWeight(previousPDF,lightPDF);
            addIncident(emissionAt(hit.primitive,hit.bary,true,origin,bounce==0 || previousPDF<0)*weight);
        }
        start=world+geometric*pc.parameters.x;
        visibilityAbsorption=mediumCount>0 ? MEDIUM_ABSORPTION(mediumCount-1):vec3(0);
        return 2u;
    }
}
