float waveValue(vec4 wave,int function,float time) {
    float x=wave.z+time*wave.w, p=fract(x), value=0;
    if(function==1) value=sin(x*2*PI);
    if(function==2) value=p<0.5 ? 1:-1;
    if(function==3) value=1-4*abs(fract(x+0.25)-0.5);
    if(function==4) value=p;
    if(function==5) value=1-p;
    if(function==6) value=mix(fract(sin(floor(x)*12.9898)*43758.5453),
        fract(sin((floor(x)+1)*12.9898)*43758.5453),smoothstep(0,1,p))*2-1;
    return wave.x+wave.y*value;
}
