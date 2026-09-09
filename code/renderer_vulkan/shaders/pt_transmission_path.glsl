// Guide-only transmission-first correspondence, including mandatory internal
// reflection. The radiance estimator retains all branches and its ray budgets.
struct OpticalInterface { vec3 point; vec3 normal; vec2 optics; uint flags; uint primitive; };

bool previousInterfaceContains(uint primitive,vec3 point) {
    uvec3 t=triangle(primitive);
    bool moving=uint(vertices[t.x].normal.w)!=0u;
    vec3 a=moving ? vertices[t.x].previous.xyz:position(t.x);
    vec3 b=moving ? vertices[t.y].previous.xyz:position(t.y);
    vec3 c=moving ? vertices[t.z].previous.xyz:position(t.z);
    vec3 u=b-a,v=c-a,w=point-a;
    float uu=dot(u,u),uv=dot(u,v),vv=dot(v,v),wu=dot(w,u),wv=dot(w,v);
    float d=uu*vv-uv*uv;
    if(d<=0.000001) return false;
    vec2 bary=vec2(vv*wu-uv*wv,uu*wv-uv*wu)/d;
    return all(greaterThanEqual(bary,vec2(-0.001))) && bary.x+bary.y<=1.001;
}

// Replay the old path over old tracked planes. Newton iterations do only math;
// finite old triangle containment is checked once, after convergence. The old
// pixel's guide/signature/target checks validate actual old visibility afterward.
bool replayTransmissionPath(vec3 firstPoint,OpticalInterface interfaces[PT_TRANSMISSION_INTERFACES],
    int count,bool finiteSurfaces,out vec3 start,out vec3 outgoing) {
    start=rp.previousOrigin.xyz;
    outgoing=normalize(firstPoint-start);
    for(int i=0;i<count;++i) {
        vec3 point=firstPoint;
        if(i>0) {
            float denominator=dot(outgoing,interfaces[i].normal);
            if(denominator>=-0.001) return false;
            float distance=dot(interfaces[i].point-start,interfaces[i].normal)/denominator;
            if(distance<=0.001) return false;
            point=start+outgoing*distance;
        }
        if(finiteSurfaces && !previousInterfaceContains(interfaces[i].primitive,point)) return false;
        vec3 nextStart,nextDirection;
        bool water=(interfaces[i].flags&1u)!=0u,entering=(interfaces[i].flags&2u)!=0u;
        if((interfaces[i].flags&4u)!=0u) {
            if(!interfaceTotalInternalReflection(point,start,interfaces[i].normal,interfaces[i].optics,
                water,entering,rp.depthProjection.w,pc.parameters.x,nextStart,nextDirection)) return false;
        } else if(!interfaceTransmission(point,start,interfaces[i].normal,interfaces[i].optics,
            water,entering,rp.depthProjection.w,pc.parameters.x,nextStart,nextDirection)) return false;
        start=nextStart; outgoing=nextDirection;
    }
    return true;
}

bool transmissionPathResidual(vec3 point,vec3 target,vec3 u,vec3 v,
    OpticalInterface interfaces[PT_TRANSMISSION_INTERFACES],int count,out vec2 residual) {
    vec3 start,outgoing;
    if(!replayTransmissionPath(point,interfaces,count,false,start,outgoing)) return false;
    vec3 facing=interfaces[count-1].normal;
    float distance=dot(target-start,facing)/dot(outgoing,facing);
    if(distance<=0) return false;
    vec3 error=start+outgoing*distance-target;
    residual=vec2(dot(error,u),dot(error,v));
    return !any(isnan(residual)) && !any(isinf(residual));
}

bool previousTransmissionPath(vec3 target,OpticalInterface interfaces[PT_TRANSMISSION_INTERFACES],
    int count,out vec3 point,out vec3 outgoing) {
    point=interfaces[0].point;
    if(count==1) {
        vec3 seed=point;
        if(!previousTransmissionPoint(seed,interfaces[0].normal,target,interfaces[0].optics,
            (interfaces[0].flags&1u)!=0u,(interfaces[0].flags&2u)!=0u,point)) return false;
        vec3 start;
        return replayTransmissionPath(point,interfaces,count,false,start,outgoing);
    }
    vec3 facing=interfaces[0].normal, lastFacing=interfaces[count-1].normal;
    float targetSide=dot(target-interfaces[count-1].point,lastFacing);
    bool lastReflects=(interfaces[count-1].flags&4u)!=0u;
    if(dot(rp.previousOrigin.xyz-point,facing)<=0.001 ||
        (lastReflects ? targetSide<=pc.parameters.x:targetSide>=-pc.parameters.x)) return false;
    vec3 u=normalize(cross(abs(facing.z)<0.999 ? vec3(0,0,1):vec3(0,1,0),facing)),v=cross(facing,u);
    vec3 endU=normalize(cross(abs(lastFacing.z)<0.999 ? vec3(0,0,1):vec3(0,1,0),lastFacing)),endV=cross(lastFacing,endU);
    float pathDistance=length(point-rp.previousOrigin.xyz)+length(target-interfaces[count-1].point);
    for(int i=1;i<count;++i) pathDistance+=length(interfaces[i].point-interfaces[i-1].point);
    float footprint=max(0.02,pathDistance*2/(float(imageSize(outputColor).y)*abs(rp.previousUp.w)));
    float epsilon=max(0.01,footprint*0.05),tolerance=max(0.005,footprint*0.1);
    for(int iteration=0;iteration<8;++iteration) {
        vec2 r,rx,ry;
        if(!transmissionPathResidual(point,target,endU,endV,interfaces,count,r)) return false;
        if(length(r)<=tolerance) break;
        if(!transmissionPathResidual(point+u*epsilon,target,endU,endV,interfaces,count,rx) ||
            !transmissionPathResidual(point+v*epsilon,target,endU,endV,interfaces,count,ry)) return false;
        mat2 jacobian=mat2((rx-r)/epsilon,(ry-r)/epsilon);
        if(abs(determinant(jacobian))<0.0001) return false;
        vec2 step=inverse(jacobian)*r;
        if(any(isnan(step)) || any(isinf(step))) return false;
        step*=min(1,128/max(length(step),0.0001));
        point-=u*step.x+v*step.y;
    }
    vec2 r;
    vec3 start;
    return transmissionPathResidual(point,target,endU,endV,interfaces,count,r) && length(r)<=tolerance &&
        replayTransmissionPath(point,interfaces,count,true,start,outgoing);
}

void transmissionPathGuide(Hit first,vec3 firstWorld,vec3 firstDirection,
    out TransmissionGuide guide,out TransmissionMotion motion) {
    guide=TransmissionGuide(vec4(0),uvec4(0));
    motion=TransmissionMotion(vec4(0),vec4(0),vec4(0));
    OpticalInterface interfaces[PT_TRANSMISSION_INTERFACES];
    float mediumIOR[4];
    int mediumCount=0;
    if(rp.cameraMedium.x>1) { mediumIOR[0]=rp.cameraMedium.x; mediumCount=1; }
    bool tracked=rp.jitterHistoryReset.w==0;
    uvec2 signature=uvec2(2166136261u,2654435769u);
    Hit hit=first;
    vec3 world=firstWorld, direction=firstDirection, camera=pc.originNear.xyz;
    int limit=min(PT_TRANSMISSION_INTERFACES,int(pc.sampling.y)-1);
    for(int depth=0;depth<limit;++depth) {
        Material m=materialProperties(triangleMaterials[hit.primitive]&0xffffu);
        bool water=m.optical.y==0 && m.optical.z!=0 && m.maps.y<0;
        if(m.params.x!=4 || m.maps.y>=0 || (m.params.w!=0 && !water) ||
            (m.optical.z!=0 && !water)) return;
        uvec3 t=triangle(hit.primitive);
        vec3 authored=vertices[t.x].normal.xyz*(1-hit.bary.x-hit.bary.y)+
            vertices[t.y].normal.xyz*hit.bary.x+vertices[t.z].normal.xyz*hit.bary.y;
        float area;
        vec3 geometric=geometricNormal(hit.primitive,area), winding=geometric;
        if(area<=0.000001 || dot(authored,authored)<=0.000001 ||
            abs(dot(normalize(authored),geometric))<=0.9999) return;
        if(dot(geometric,direction)>0) geometric=-geometric;
        bool entering=dot(authored,direction)<0;
        float etaI=mediumCount>0 ? mediumIOR[mediumCount-1]:1;
        float etaT=entering ? m.optical.x:(mediumCount>1 ? mediumIOR[mediumCount-2]:1);
        if(!entering && mediumCount==0 && m.optical.y==0) {
            etaI=m.optical.x; mediumIOR[0]=etaI; mediumCount=1;
        }
        if(m.optical.y>0) etaT=m.optical.x;
        vec2 optics=vec2(etaI/etaT,m.optical.y);
        uint flags=(water ? 1u:0u)|(entering ? 2u:0u);
        vec3 start,outgoing;
        bool internalReflection=interfaceTotalInternalReflection(world,camera,geometric,optics,water,entering,
            rp.depthProjection.z,pc.parameters.x,start,outgoing);
        if(internalReflection) {
            if(depth==0) return; // A primary TIR ray belongs to reflection, not transmission.
            flags|=4u;
        } else if(!interfaceTransmission(world,camera,geometric,optics,water,entering,rp.depthProjection.z,
            pc.parameters.x,start,outgoing)) return;
        ReflectionGuide oldSurface=hitCorrespondence(hit,world,direction,true);
        vec3 oldFacing=geometric;
        if(uint(vertices[t.x].normal.w)!=0u) {
            vec3 crossEdges=cross(vertices[t.y].previous.xyz-vertices[t.x].previous.xyz,
                vertices[t.z].previous.xyz-vertices[t.x].previous.xyz);
            // Preserve the current face's winding sign under motion, rather
            // than orienting an old plane using today's incoming direction.
            oldFacing=crossEdges/max(length(crossEdges),0.000001)*(dot(winding,geometric)<0 ? -1:1);
        }
        tracked=tracked && oldSurface.position.w>0;
        interfaces[depth]=OpticalInterface(oldSurface.position.xyz,oldFacing,optics,flags,hit.primitive);
        signature=transmissionSignature(signature,floatBitsToUint(oldSurface.normal.w));
        signature=transmissionSignature(signature,flags);
        signature=transmissionSignature(signature,floatBitsToUint(optics.x));
        signature=transmissionSignature(signature,floatBitsToUint(optics.y));
        for(int channel=0;channel<3;++channel) signature=transmissionSignature(signature,floatBitsToUint(m.absorption[channel]));
        // Intermediate finite faces distinguish separate panes sharing a world
        // material. Rebatching can conservatively reject otherwise usable history.
        if(depth>0) signature=transmissionSignature(signature,hit.primitive);
        if(m.optical.y==0 && !internalReflection) {
            if(entering) {
                if(mediumCount>=4) return;
                mediumIOR[mediumCount++]=etaT;
            } else mediumCount=max(mediumCount-1,0);
        }
        Hit next;
        if(!trace(start,outgoing,0.001,pc.parameters.y,0u,next)) return;
        vec3 target=start+outgoing*next.distance;
        Material targetMaterial=materialProperties(triangleMaterials[next.primitive]&0xffffu);
        if(targetMaterial.params.x==0 && targetMaterial.params.w==0 && targetMaterial.emission.z==0) {
            ReflectionGuide now=hitCorrespondence(next,target,outgoing,false);
            ReflectionGuide before=hitCorrespondence(next,target,outgoing,true);
            guide=TransmissionGuide(vec4(now.position.xyz,packReflectionNormal(now.normal.xyz)),
                uvec4(floatBitsToUint(now.normal.w),signature,uint(depth+1)));
            motion.position=before.position;
            motion.normal=before.normal;
            vec3 previousPoint,previousOutgoing;
            if(tracked && before.position.w>0 && previousTransmissionPath(before.position.xyz,interfaces,depth+1,
                previousPoint,previousOutgoing)) {
                motion.projection=vec4(previousPoint,1);
                motion.normal=hitCorrespondence(next,target,previousOutgoing,true).normal;
            }
            return;
        }
        if(targetMaterial.params.x!=4) return; // Layered effects need their own path model.
        hit=next; world=target; direction=outgoing; camera=start;
    }
    // A longer chain is deliberately invalid; never reuse a truncated path.
}
