/* Bounded, non-executable manifest data. Shared with offline tests. */
#ifndef VQ3_POSTFX_PARSE_H
#define VQ3_POSTFX_PARSE_H
#include "postfx.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static inline int PFX_Id(const char *s) {
    size_t n=0;
    if (!s || !*s) return 0;
    for (;s[n];++n) if (!((s[n]>='a' && s[n]<='z') || (s[n]>='0' && s[n]<='9'))) return 0;
    return n<PFX_ID;
}
static inline int PFX_File(const char *s) {
    size_t n=0;
    if (!s || !*s || *s=='.' || strstr(s,"..")) return 0;
    for (;s[n];++n) if (!((s[n]>='a' && s[n]<='z') || (s[n]>='0' && s[n]<='9') || strchr("._-",s[n]))) return 0;
    return n<PFX_FILE;
}
static inline int PFX_TextureFile(const char *s) {
    if (!PFX_File(s)) return 0;
    size_t n=strlen(s);
    return n>4 && !strcmp(s+n-4,".png");
}
/* Lexical plus bounded double checks remain valid under release fast-math. */
static inline int PFX_Number(const char *s, float *out) {
    const char *p=s; char *end; double d;
    if (!*p) return 0;
    for (;*p;++p) if (!strchr("0123456789.+-eE",*p)) return 0;
    d=strtod(s,&end);
    if (*end || d < -65536.0 || d > 65536.0) return 0;
    *out=(float)d; return 1;
}
static inline int PFX_Cvar(const char *name) {
    char id[PFX_ID]; const char *split; size_t n;
    if (strncmp(name,"r_fx_",5)) return 0;
    split=strchr(name+5,'_'); if (!split) return 0;
    n=(size_t)(split-name-5); if (!n || n>=sizeof(id)) return 0;
    memcpy(id,name+5,n); id[n]=0;
    return PFX_Id(id) && PFX_Id(split+1);
}
/* Tokens accept quoted labels and // comments, never escapes or commands. */
static inline int PFX_Token(const char **cursor, char *out, size_t cap) {
    const char *p=*cursor; size_t n=0; int quote;
    for (;;) {
        while (*p && (unsigned char)*p<=32) ++p;
        if (p[0]=='/' && p[1]=='/') { while (*p && *p!='\n') ++p; } else break;
    }
    if (!*p) { *cursor=p; *out=0; return 0; }
    quote=*p=='"'; if (quote) ++p;
    while (*p && (quote ? *p!='"' : (unsigned char)*p>32)) {
        if ((unsigned char)*p<32 || *p=='\\' || n+1>=cap) return -1;
        out[n++]=*p++;
    }
    if (quote) { if (*p!='"') return -1; ++p; }
    out[n]=0; *cursor=p; return 1;
}
static inline int PFX_Parse(const char *id, const char *text, postfxEffect_t *out) {
    postfxEffect_t e; const char *p=text; char t[128]; int result, named=0,version;
    memset(&e,0,sizeof(e));
    if (!PFX_Id(id)) return 0;
    strcpy(e.id,id);
#define PFX_READ(dst) do { if (PFX_Token(&p,(dst),sizeof(dst))!=1) return 0; } while (0)
    PFX_READ(t); if (strcmp(t,"version")) return 0;
    PFX_READ(t); version=!strcmp(t,"1") ? 1 : !strcmp(t,"2") ? 2 : !strcmp(t,"3") ? 3 : !strcmp(t,"4") ? 4 : !strcmp(t,"5") ? 5 : 0;
    if (!version) return 0;
    while ((result=PFX_Token(&p,t,sizeof(t)))==1) {
        if (!strcmp(t,"name")) {
            if (named++) return 0;
            PFX_READ(e.label); if (!e.label[0]) return 0;
        } else if (!strcmp(t,"texture")) {
            if (version<2 || e.texture[0]) return 0;
            PFX_READ(e.texture); if (!PFX_TextureFile(e.texture)) return 0;
        } else if (!strcmp(t,"downsample")) {
            if (version!=3 || e.downsample) return 0;
            PFX_READ(t); if (strcmp(t,"4")) return 0;
            e.downsample=4;
        } else if (!strcmp(t,"depth")) {
            if (version<4 || e.depth) return 0;
            PFX_READ(t); if (strcmp(t,"scene")) return 0;
            e.depth=1;
        } else if (!strcmp(t,"motion")) {
            if (version!=5 || e.motion) return 0;
            PFX_READ(t); if (strcmp(t,"scene")) return 0;
            e.motion=1;
        } else if (!strcmp(t,"pass")) {
            postfxPass_t *pass;
            if (e.numPasses==PFX_MAX_PASSES) return 0;
            pass=&e.passes[e.numPasses++]; PFX_READ(t);
            if (!strcmp(t,"compute")) pass->compute=1;
            else if (!strcmp(t,"graphics")) { PFX_READ(pass->vertex); if (!PFX_File(pass->vertex)) return 0; }
            else return 0;
            PFX_READ(pass->shader); if (!PFX_File(pass->shader)) return 0;
        } else if (!strcmp(t,"param")) {
            postfxParam_t *param; float *numbers[4]; int j;
            if (e.numParams==PFX_MAX_PARAMS) return 0;
            param=&e.params[e.numParams]; PFX_READ(param->id); PFX_READ(param->label);
            if (!PFX_Id(param->id) || !param->label[0] || !strcmp(param->id,"enabled") || !strcmp(param->id,"order")) return 0;
            for (j=0;j<e.numParams;++j) if (!strcmp(e.params[j].id,param->id)) return 0;
            numbers[0]=&param->initial; numbers[1]=&param->minimum; numbers[2]=&param->maximum; numbers[3]=&param->step;
            for (j=0;j<4;++j) { PFX_READ(t); if (!PFX_Number(t,numbers[j])) return 0; }
            if (param->minimum>=param->maximum || param->initial<param->minimum || param->initial>param->maximum ||
                param->step<=0 || param->step>param->maximum-param->minimum) return 0;
            ++e.numParams;
        } else return 0;
    }
#undef PFX_READ
    if (result<0 || !named || !e.numPasses) return 0;
    if (version==3) {
        if (e.downsample!=4 || e.numPasses<2) return 0;
        // The reduced prepasses are float graphics targets. Only the final
        // pass writes full-size color; the effect's input stays untouched.
        for (int i=0;i<e.numPasses;++i) if (e.passes[i].compute) return 0;
    }
    if (version>=4 && !e.depth) return 0;
    if (version==5 && !e.motion) return 0;
    *out=e; return 1;
}
static inline int PFX_PassDivisor(const postfxEffect_t *e,int pass) {
    return e->downsample && pass<e->numPasses-1 ? e->downsample:1;
}
// 0/1: full-size chain targets. 2/3: shared quarter-size float prepasses.
// Never read and write one image, or overwrite a v3 effect's original color.
static inline int PFX_PassTarget(const postfxEffect_t *e,int pass,int input,int original) {
    int first=PFX_PassDivisor(e,pass)>1 ? 2:0;
    for (int i=first;i<first+2;++i)
        if (i!=input && (!e->downsample || i!=original)) return i;
    return -1;
}
#endif
