/* Restricted v1-v5 shader interface check; still require spirv-val on packages.
 * Not a general SPIR-V validator and not a sandbox for untrusted GPU programs. */
#ifndef PFX_SPIRV_H
#define PFX_SPIRV_H
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
static int PFX_SpirvGuides(const uint32_t *w,size_t n,uint32_t model,int auxiliary,int original,int depth,int motion) {
    uint32_t *defs=NULL,*bindings=NULL,*sets=NULL,*offsets=NULL;
    uint32_t bound,entry=0,workgroup=0; int valid=0,entries=0,local=0,function=0,mainFound=0;
    size_t k;
    const uint32_t pushVectors=motion ? 8u:depth ? 4u:3u;
    if (n<5 || w[0]!=0x07230203u || w[1]!=0x00010000u || w[4] || !w[3] || w[3]>8192) return 0;
    bound=w[3]; defs=calloc(bound*4,sizeof(*defs)); if (!defs) return 0;
    bindings=defs+bound; sets=bindings+bound; offsets=sets+bound;
    for (k=5;k<n;) {
        uint32_t len=w[k]>>16,op=w[k]&65535,id=0;
        if (!len || len>n-k) goto done;
        if (op==17 && (len!=2 || (w[k+1]!=1 && w[k+1]!=50))) goto done; // Shader, ImageQuery only.
        if (op==54) { if (len!=5 || function) goto done; function=1; if (w[k+2]==entry) mainFound=1; }
        if (op==56) { if (len!=1 || !function) goto done; function=0; }
        if (op==15) {
            if (len<5 || w[k+1]!=model || w[k+3]!=0x6e69616du || w[k+4] || entries++) goto done;
            entry=w[k+2];
        }
        if (op>=19 && op<=33 && len>=2) id=w[k+1]; // Type declarations.
        if ((op==43 || op==44 || op==59) && len>=3) id=w[k+2];
        if (id) { if (id>=bound || defs[id]) goto done; defs[id]=(uint32_t)k; }
        if (op==71) {
            if (len<3 || w[k+1]>=bound) goto done;
            id=w[k+1];
            if (w[k+2]==33 || w[k+2]==34 || w[k+2]==11) {
                if (len!=4) goto done;
                if (w[k+2]==33) {
                    if (w[k+3]>5u || (w[k+3]==2u && !auxiliary) || (w[k+3]==3u && !original) ||
                        (w[k+3]==4u && !depth) || (w[k+3]==5u && !motion) || bindings[id]) goto done;
                    bindings[id]=w[k+3]+1;
                }
                if (w[k+2]==34) { if (w[k+3]!=0 || sets[id]) goto done; sets[id]=1; }
                if (w[k+2]==11 && w[k+3]==25) { if (workgroup) goto done; workgroup=id; }
            }
        }
        if (op==72 && len>=4 && w[k+3]==35) { // v4 adds projection/jitter at offset 48.
            if (len!=5 || w[k+1]>=bound) goto done;
            if (w[k+2]<pushVectors && w[k+4]==16*w[k+2]) offsets[w[k+1]]|=1u<<w[k+2];
        }
        k+=len;
    }
    if (entries!=1 || function || !mainFound) goto done;
#define PFX_DEF(id,op) ((id)<bound && defs[id] && (w[defs[id]]&65535)==(op))
    for (k=5;k<n;k+=w[k]>>16) {
        uint32_t len=w[k]>>16,op=w[k]&65535;
        if (op==16 && len>=3 && w[k+2]==17) {
            if (len!=6 || w[k+1]!=entry || w[k+3]!=8 || w[k+4]!=8 || w[k+5]!=1) goto done;
            local=1;
        }
        if (op==59) {
            uint32_t type,ptr,id,storage;
            if (len<4) goto done;
            ptr=w[k+1]; id=w[k+2]; storage=w[k+3];
            if (!PFX_DEF(ptr,32) || (w[defs[ptr]]>>16)!=4 || w[defs[ptr]+2]!=storage) goto done;
            type=w[defs[ptr]+3];
            if (storage==0) { // Exactly sampler2D scene / rgba8 image2D result, no arrays/buffers.
                uint32_t image=type,at;
                if (model==0 || !bindings[id] || !sets[id]) goto done;
                if (bindings[id]!=2) {
                    if (!PFX_DEF(type,27) || (w[defs[type]]>>16)!=3) goto done;
                    image=w[defs[type]+2];
                } else if (model!=5) goto done;
                if (!PFX_DEF(image,25)) goto done;
                at=defs[image];
                if ((w[at]>>16)!=9 || w[at+3]!=1 || w[at+4] || w[at+5] || w[at+6] ||
                    w[at+7]!=(bindings[id]!=2 ? 1u:2u) || w[at+8]!=(bindings[id]!=2 ? 0u:4u)) goto done;
                type=w[at+2];
                if (!PFX_DEF(type,22) || (w[defs[type]]>>16)!=3 || w[defs[type]+2]!=32) goto done;
            } else if (storage==9) {
                uint32_t at;
                if (!PFX_DEF(type,30) || (w[defs[type]]>>16)!=pushVectors+2 || offsets[type]!=((1u<<pushVectors)-1)) goto done;
                at=defs[type];
                for (uint32_t m=0;m<pushVectors;++m) {
                    uint32_t vec=w[at+2+m],scalar;
                    if (!PFX_DEF(vec,23) || (w[defs[vec]]>>16)!=4 || w[defs[vec]+3]!=4) goto done;
                    scalar=w[defs[vec]+2];
                    if (!PFX_DEF(scalar,22) || (w[defs[scalar]]>>16)!=3 || w[defs[scalar]+2]!=32) goto done;
                }
            } else if (storage!=1 && storage!=3 && storage!=4 && storage!=6 && storage!=7) goto done;
        }
    }
    if (model==5 && !local) goto done;
    if (workgroup) {
        uint32_t at;
        if (model!=5 || !PFX_DEF(workgroup,44) || (w[defs[workgroup]]>>16)!=6) goto done;
        at=defs[workgroup];
        for (int i=0;i<3;++i) {
            uint32_t id=w[at+3+i];
            if (!PFX_DEF(id,43) || (w[defs[id]]>>16)!=4 || w[defs[id]+3]!=(i==2 ? 1u:8u)) goto done;
        }
    }
    valid=1;
done:
#undef PFX_DEF
    free(defs); return valid;
}
static inline int PFX_SpirvInputs(const uint32_t *w,size_t n,uint32_t model,int auxiliary,int original,int depth) {
    return PFX_SpirvGuides(w,n,model,auxiliary,original,depth,0);
}
static inline int PFX_Spirv(const uint32_t *w,size_t n,uint32_t model) {
    return PFX_SpirvInputs(w,n,model,0,0,0);
}
static inline int PFX_SpirvResources(const uint32_t *w,size_t n,uint32_t model,int auxiliary,int original) {
    return PFX_SpirvInputs(w,n,model,auxiliary,original,0);
}
static inline int PFX_SpirvTexture(const uint32_t *w,size_t n,uint32_t model,int auxiliary) {
    return PFX_SpirvResources(w,n,model,auxiliary,0);
}
#endif
