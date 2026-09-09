// Quake III's tr_sky.c face order/orientation, not Vulkan cubemap conventions.
// Return indices in ParseSkyParms order: rt, bk, lf, ft, up, dn.
int skyFace(vec3 d) {
    vec3 a=abs(d);
    if(a.x>=a.y && a.x>=a.z) return d.x>=0 ? 0:2;
    if(a.y>=a.z) return d.y>=0 ? 1:3;
    return d.z>=0 ? 4:5;
}
vec2 skyFaceUV(vec3 d,int face) {
    vec2 st;
    if(face==0) st=vec2(-d.y,d.z)/max(abs(d.x),0.000001);
    else if(face==2) st=vec2(d.y,d.z)/max(abs(d.x),0.000001);
    else if(face==1) st=vec2(d.x,d.z)/max(abs(d.y),0.000001);
    else if(face==3) st=vec2(-d.x,d.z)/max(abs(d.y),0.000001);
    else if(face==4) st=vec2(-d.y,-d.x)/max(abs(d.z),0.000001);
    else st=vec2(-d.y,d.x)/max(abs(d.z),0.000001);
    return vec2(st.x+1,1-st.y)*0.5;
}
vec2 skyCloudUV(vec3 direction,float height) {
    // Continuous equivalent of R_InitSkyTexCoords' radius-4096 cloud shell.
    // Rationalized upward intersection avoids cancellation for low clouds.
    vec3 d=normalize(direction);
    float radius=4096,h=max(height,1),shell=h*(2*radius+h);
    float z=radius*d.z,root=sqrt(z*z+shell);
    float distance=z>=0 ? shell/(root+z):root-z;
    vec3 point=normalize(d*distance+vec3(0,0,radius));
    return acos(clamp(point.xy,vec2(-1),vec2(1)));
}
