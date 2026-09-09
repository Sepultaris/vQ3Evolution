// Frozen pre-cache modifier loop for the CPU equivalence fixture.
    for(int i=0;!context.uniformTint && i<int(layer.params.w);++i) {
        TexMod m=materials[id].layers[layerIndex].mods[i];
        int type=int(m.a.x);
        if(type==1) {
            uv=vec2(dot(vec3(uv,1),m.a.yzw),dot(vec3(uv,1),m.b.xyz));
            gradients=mat2(m.a.y,m.b.x,m.a.z,m.b.y)*gradients;
        }
        if(type==2) {
            vec2 phase=(vec2(local.x+local.z,local.y)/1024+m.wave.z+time*m.wave.w)*2*PI;
            uv+=sin(phase)*m.wave.y;
            vec2 slope=cos(phase)*(m.wave.y*2*PI/1024);
            gradients[0]+=slope*vec2(localDx.x+localDx.z,localDx.y);
            gradients[1]+=slope*vec2(localDy.x+localDy.z,localDy.y);
        }
        if(type==3) uv+=fract(m.a.yz*time);
        if(type==4) { uv*=m.a.yz; gradients[0]*=m.a.yz; gradients[1]*=m.a.yz; }
        if(type==5) {
            float stretch=waveValue(m.wave,int(m.b.w),time);
            if(abs(stretch)>0.00001) { uv=(uv-0.5)/stretch+0.5; gradients/=stretch; }
        }
        if(type==6) {
            float angle=-m.a.y*time*PI/180, c=cos(angle), s=sin(angle);
            uv=mat2(c,s,-s,c)*(uv-0.5)+0.5;
            gradients=mat2(c,s,-s,c)*gradients;
        }
        if(type==7) uv+=fract(context.timeScroll.yz*time);
    }
