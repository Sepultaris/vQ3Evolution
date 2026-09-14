/* VQ3 Evolution presentation panel and global profile, owned by the engine.
 * Mod menus/modules remain intact, including on pure servers. GPL-2.0-or-later. */
#include "client.h"
#include "cl_options.h"
#include "../qcommon/release_defaults.h"
#include "../renderercommon/postfx_parse.h"

#define OPTIONS_FILE "vq3e-rendering.cfg"
#define OPTIONS_MAX 256
#define OPTIONS_VALUE 64
#define OPTIONS_PROFILE_SIZE 65536
#define OPTIONS_VISIBLE 10
typedef struct {
    const char *name, *label, *initial;
    int page;
    float minimum, maximum, step;
    const char *choices; // Pipe-separated values; numeric controls use NULL.
    const char *labels;
    qboolean restart;
} optionDef_t;
static const optionDef_t baseOptionDefs[] = {
    {"cl_renderer","Renderer","vulkan",0,0,0,0,"vulkan|opengl1|opengl2","Vulkan|OpenGL 1|OpenGL 2",qtrue},
    {"r_fullscreen","Fullscreen","0",0,0,1,1,"0|1","Off|On",qtrue},
    {"r_mode","Resolution mode","-1",0,-1,0,0,"-1|-2","Custom size|Desktop size",qtrue},
    {"r_customwidth","Custom width","1920",0,640,7680,16,NULL,NULL,qtrue},
    {"r_customheight","Custom height","1080",0,480,4320,8,NULL,NULL,qtrue},
    {"r_swapInterval","VSync","0",0,0,1,1,"0|1","Off|On",qtrue},
    {"cg_fov","Horizontal FOV (4:3)","90",0,1,160,.5f,NULL,NULL,qfalse},
    {"r_textureMode","Texture filtering","GL_LINEAR_MIPMAP_NEAREST",0,0,0,0,
        "GL_LINEAR_MIPMAP_NEAREST|GL_LINEAR_MIPMAP_LINEAR","Bilinear|Trilinear",qfalse},
    {"r_ext_texture_filter_anisotropic","Anisotropic filtering","0",0,0,1,1,"0|1","Off|On",qtrue},
    {"r_ext_max_anisotropy","Anisotropy level","2",0,1,16,1,"1|2|4|8|16","1x|2x|4x|8x|16x",qtrue},
    {"r_rayTracing","Lighting mode","0",1,0,2,1,"0|2","Raster|Path tracing",qtrue},
    {"r_pathTracingSamples","Samples per pixel","2",1,1,64,1,NULL,NULL,qfalse},
    {"r_pathTracingBounces","Maximum bounces","4",1,1,12,1,NULL,NULL,qfalse},
    {"r_pathTracingAdaptive","Adaptive sampling","0",1,0,1,1,"0|1","Off|On",qfalse},
    {"r_pathTracingAmbient","Ambient light","0",1,0,2,.01f,NULL,NULL,qfalse},
    {"r_pathTracingSunAngle","Sun penumbra (degrees)","0",1,0,20,.1f,NULL,NULL,qfalse},
    {"r_pathTracingLightRadius","Local light radius","0",1,0,64,.5f,NULL,NULL,qfalse},
    {"r_dlss","DLSS / anti-aliasing","0",2,0,5,1,"0|1|2|3|4|5",
        "Off|Quality|Balanced|Performance|Ultra Performance|DLAA",qtrue},
    {"r_dlssRayReconstruction","Ray reconstruction","1",2,0,1,1,"0|1","Off|On",qtrue},
    {"r_dlssFrameGeneration","Frame generation","0",2,0,1,1,"0|1","Off|On",qtrue},
    {"r_dlssFrameGenerationMultiplier","Frame generation multiplier","2",2,2,6,1,NULL,NULL,qtrue},
    // Neural rendering (WIP) stays console-only and in the shared profile.
    {"r_dlssNeuralRendering",NULL,"0",-1,0,3,1,"0|1|2|3",NULL,qtrue},
    {"r_reflex","NVIDIA Reflex","1",2,0,2,1,"0|1|2","Off|On|On + Boost",qtrue},
    {"r_dlssSharpness","DLSS sharpness","0",2,0,1,.05f,NULL,NULL,qfalse},
    {"r_dlssNRIntensity",NULL,"1",-1,0,2,.05f,NULL,NULL,qfalse},
    {"r_dlssNRLocalToneStrength",NULL,"1",-1,0,2,.05f,NULL,NULL,qfalse},
    {"r_dlssNRLocalStructureStrength",NULL,"1",-1,0,2,.05f,NULL,NULL,qfalse},
    {"r_dlssNRSkinStructureStrength",NULL,"1",-1,0,2,.05f,NULL,NULL,qfalse},
    {"ui_scale","Menu scale","1",3,.5f,1.5f,.05f,NULL,NULL,qfalse},
    {"cg_hudScale","HUD scale","1",3,.5f,1.5f,.05f,NULL,NULL,qfalse},
    {"cl_legacyUIScale","Legacy menu scaling","1",3,0,2,1,"1|0|2","Automatic|Disabled|Force engine scaling",qfalse},
    {"cl_legacyHUDScale","Legacy HUD scaling","1",3,0,2,1,"1|0|2","Automatic|Disabled|Force engine scaling",qfalse},
    // Persist additional presentation values without crowding the panel.
    {"r_muzzleFlashBrightness",NULL,"1",-1,0,8,.05f,NULL,NULL,qfalse},
    {"r_muzzleFlashLightScale",NULL,"1",-1,0,8,.05f,NULL,NULL,qfalse},
    {"r_rocketBrightness",NULL,"1",-1,0,8,.05f,NULL,NULL,qfalse},
    {"r_rocketLightScale",NULL,"1",-1,0,8,.05f,NULL,NULL,qfalse},
    {"r_rocketExplosionLightScale",NULL,"1",-1,0,8,.05f,NULL,NULL,qfalse},
    {"r_lightningGunLightScale",NULL,"1",-1,0,8,.05f,NULL,NULL,qfalse},
    {"r_rocketExplosionLightScale",NULL,"1",-1,0,8,.05f,NULL,NULL,qfalse},
    {"r_lightningGunLightScale",NULL,"1",-1,0,8,.05f,NULL,NULL,qfalse},
    {"r_picmip",NULL,"0",-1,0,16,1,NULL,NULL,qtrue},
    {"r_displayIndex",NULL,"0",-1,0,16,1,NULL,NULL,qtrue},
    {"r_displayRefresh",NULL,"0",-1,0,1000,1,NULL,NULL,qtrue},
    {"r_postfx","Post-effect system","0",4,0,1,1,"0|1","Off|On (restart to apply)",qtrue},
    {"r_pathTracingExposure","Base exposure (multiplier)","1",5,.01f,16,.01f,NULL,NULL,qfalse},
    {"r_pathTracingAutoExposure","Automatic exposure","0",5,0,1,1,"0|1","Off|On",qfalse},
    {"r_pathTracingAdaptiveSpeed","Adaptation time (seconds)",".25",5,.01f,10,.01f,NULL,NULL,qfalse},
    {"r_pathTracingAdaptiveTarget","Auto target (0.18 = neutral)",".18",5,.0001f,20,.0001f,NULL,NULL,qfalse},
    {"r_pathTracingAdaptiveMin","Minimum auto exposure",".05",5,0,64,.01f,NULL,NULL,qfalse},
    {"r_pathTracingAdaptiveMax","Maximum auto exposure","16",5,0,4096,.1f,NULL,NULL,qfalse}
};
static optionDef_t optionDefs[OPTIONS_MAX];
static int optionCount;
static char effectText[OPTIONS_MAX][3][OPTIONS_VALUE];
static postfxEffect_t effectInfo[PFX_MAX_EFFECTS];
static int effectCount;
static int effectOwner[OPTIONS_MAX], effectHeader[PFX_MAX_EFFECTS];
static qboolean effectExpanded[PFX_MAX_EFFECTS];
static const char *pageNames[] = {"DISPLAY", "LIGHTING", "NVIDIA", "INTERFACE", "EFFECTS", "EXPOSURE"};
static struct {
    qboolean active, swallowing[MAX_KEYS], dragging;
    int page, selected;
    float mouseX, mouseY, scale, left, top;
    char pending[OPTIONS_MAX][OPTIONS_VALUE], original[OPTIONS_MAX][OPTIONS_VALUE];
    char status[128];
} options;
static qboolean moduleScales[2]; // cgame / UI; reset for every VM initialization.
static double mouseRemainder[2][2];
static cvar_t *legacyMode[2], *legacyScale[2];
static qboolean profileSafeMode;

static void OptionsBuildDefinitions(qboolean effects) {
    int i,p;
    memcpy(optionDefs,baseOptionDefs,sizeof(baseOptionDefs));
    optionCount=ARRAY_LEN(baseOptionDefs); effectCount=0;
    for (i=0;i<optionCount;++i)
        optionDefs[i].initial=VQ3E_DefaultValue(optionDefs[i].name,optionDefs[i].initial);
    for (i=0;i<OPTIONS_MAX;++i) effectOwner[i]=-1;
    memset(effectExpanded,0,sizeof(effectExpanded));
    if (!effects || !re.GetPostFXEffect) return;
    for (i=0;i<PFX_MAX_EFFECTS && optionCount<OPTIONS_MAX && re.GetPostFXEffect(i,&effectInfo[i]);++i) {
        postfxEffect_t *e=&effectInfo[i];
        effectHeader[i]=optionCount;
        ++effectCount;
        for (p=-2;p<e->numParams && optionCount<OPTIONS_MAX;++p) {
            int slot=optionCount++; optionDef_t *d=&optionDefs[slot];
            const postfxParam_t *param=p>=0 ? &e->params[p]:NULL;
            effectOwner[slot]=i;
            memset(d,0,sizeof(*d));
            Com_sprintf(effectText[slot][0],OPTIONS_VALUE,"r_fx_%s_%s",e->id,p==-2 ? "enabled":p==-1 ? "order":param->id);
            Q_strncpyz(effectText[slot][1],p==-2 ? e->label:p==-1 ? "Order":param->label,OPTIONS_VALUE);
            Com_sprintf(effectText[slot][2],OPTIONS_VALUE,"%.9g",p==-2 ? 0:p==-1 ? 50:param->initial);
            d->name=effectText[slot][0]; d->label=effectText[slot][1]; d->initial=effectText[slot][2]; d->page=4;
            d->initial=VQ3E_DefaultValue(d->name,d->initial);
            d->minimum=param ? param->minimum:0; d->maximum=param ? param->maximum:p==-2 ? 1:100;
            d->step=param ? param->step:1;
            if (p==-2) { d->choices="0|1"; d->labels="Off|On"; }
        }
    }
}

static qboolean OptionToken(const char *list, int index, char *out, int size) {
    const char *end;
    if (!list || index < 0) return qfalse;
    while (index-- > 0) {
        list = strchr(list, '|');
        if (!list) return qfalse;
        ++list;
    }
    end = strchr(list, '|');
    Q_strncpyz(out, list, MIN(size, (int)(end ? end-list : strlen(list))+1));
    return qtrue;
}
static int OptionIndex(const char *list, const char *value) {
    char token[OPTIONS_VALUE];
    int i;
    for (i=0; OptionToken(list,i,token,sizeof(token)); ++i)
        if (!Q_stricmp(token,value)) return i;
    return -1;
}
static qboolean OptionValid(int i, const char *value) {
    const optionDef_t *d = &optionDefs[i];
    char *end;
    double number;
    const char *p;
    if (!value || !*value || strlen(value)>=OPTIONS_VALUE) return qfalse;
    // Reject NaN/Inf lexically too: release builds enable -ffast-math.
    if (!d->choices || !strcmp(d->name,"r_mode"))
        for (p=value;*p;++p) if (!strchr("0123456789.+-eE",*p)) return qfalse;
    // Existing indexed video modes remain valid until the user changes them.
    if (!strcmp(d->name,"r_mode")) {
        number=strtod(value,&end);
        return !*end && isfinite(number) && number>=-2 && number<=100 && floor(number)==number;
    }
    // Preserve console-only software tracing (WIP), without offering it in UI.
    if (!strcmp(d->name,"r_rayTracing") && !strcmp(value,"1")) return qtrue;
    if (d->choices) return OptionIndex(d->choices,value)>=0;
    number=strtod(value,&end);
    return !*end && isfinite(number) && number>=d->minimum && number<=d->maximum &&
        (d->step!=1 || floor(number)==number);
}

// Plain whitelisted data, NEVER exec'd. The home-root profile is independent
// of fs_game and cannot contain bindings, connection commands or mod logic.
void CL_OptionsLoadGlobal(qboolean safeMode) {
    fileHandle_t file;
    char data[OPTIONS_PROFILE_SIZE], name[OPTIONS_VALUE], value[OPTIONS_VALUE], *cursor;
    int length, i;
    // Safe startup must neither load nor overwrite the user's working profile,
    // even if a mod switch happens later in the same recovery session.
    profileSafeMode |= safeMode;
    if (profileSafeMode) return;
    if (!optionCount) OptionsBuildDefinitions(qfalse);
    length=FS_SV_FOpenFileRead(OPTIONS_FILE,&file);
    if (!file) return;
    if (length<=0 || length>=(int)sizeof(data)) { FS_FCloseFile(file); return; }
    FS_Read(data,length,file); FS_FCloseFile(file); data[length]=0;
    cursor=data;
    while (cursor && *cursor) {
        char *line=cursor, *end=strpbrk(cursor,"\r\n");
        const char *token;
        cursor=end ? end+1:NULL;
        if (end) *end=0;
        token=COM_ParseExt(&line,qfalse);
        if (!*token || strlen(token)>=sizeof(name)) continue;
        Q_strncpyz(name,token,sizeof(name));
        token=COM_ParseExt(&line,qfalse);
        if (!*token || strlen(token)>=sizeof(value)) continue;
        Q_strncpyz(value,token,sizeof(value));
        if (*COM_ParseExt(&line,qfalse)) continue; // Exactly two data fields.
        // Only safe numeric effect preferences, never executable commands.
        // Preserve them before the renderer/catalog exists and across mods.
        if (PFX_Cvar(name)) {
            float number;
            if (PFX_Number(value,&number)) { Cvar_Get(name,value,CVAR_ARCHIVE); Cvar_Set(name,value); }
            continue;
        }
        for (i=0;i<optionCount;++i)
            if (!strcmp(name,optionDefs[i].name) && OptionValid(i,value)) {
                Cvar_Set(name,value); break;
            }
    }
    Com_Printf("VQ3 options: shared presentation profile loaded\n");
}
static char *effectProfile;
static qboolean effectProfileOverflow;
static void OptionsSaveEffect(const char *name) {
    const char *value; char line[OPTIONS_VALUE*2+4]; float number;
    if (!PFX_Cvar(name)) return;
    value=Cvar_PendingString(name);
    if (strlen(value)>=OPTIONS_VALUE || !PFX_Number(value,&number)) return;
    Com_sprintf(line,sizeof(line),"%s %s\n",name,value);
    if (strlen(effectProfile)+strlen(line)>=OPTIONS_PROFILE_SIZE) { effectProfileOverflow=qtrue; return; }
    Q_strcat(effectProfile,OPTIONS_PROFILE_SIZE,line);
}
qboolean CL_OptionsSaveGlobal(void) {
    fileHandle_t file;
    char data[OPTIONS_PROFILE_SIZE], line[OPTIONS_VALUE*2+4];
    int i, length, written;
    if (profileSafeMode || !com_cl_running || !com_cl_running->integer) return qfalse;
    Q_strncpyz(data,"// VQ3 Evolution shared presentation settings; not an executable config.\n",sizeof(data));
    for (i=0;i<optionCount;++i) {
        if (PFX_Cvar(optionDefs[i].name)) continue;
        const char *v=Cvar_PendingString(optionDefs[i].name);
        if (OptionValid(i,v)) {
            Com_sprintf(line,sizeof(line),"%s %s\n",optionDefs[i].name,v);
            Q_strcat(data,sizeof(data),line);
        }
    }
    effectProfile=data; effectProfileOverflow=qfalse;
    Cvar_CommandCompletion(OptionsSaveEffect);
    effectProfile=NULL;
    if (effectProfileOverflow) { Com_Printf("VQ3 options: effect profile too large; previous file preserved\n"); return qfalse; }
    file=FS_SV_FOpenFileWrite(OPTIONS_FILE ".tmp");
    if (!file) {
        Com_Printf("VQ3 options: could not write the shared profile\n");
        return qfalse;
    }
    // Unbuffered, checked write: a full disk must not replace a working profile
    // with a truncated temporary file during fclose's implicit flush.
    FS_ForceFlush(file);
    length=strlen(data);
    written=FS_Write(data,length,file);
    FS_FCloseFile(file);
    if (written!=length) {
        Com_Printf("VQ3 options: incomplete profile write; previous file preserved\n");
        return qfalse;
    }
    if (!FS_SV_ReplaceFile(OPTIONS_FILE ".tmp",OPTIONS_FILE)) {
        Com_Printf("VQ3 options: could not replace the shared profile; previous file preserved\n");
        return qfalse;
    }
    return qtrue;
}

qboolean CL_OptionsActive(void) { return options.active; }
static void OptionsClose(void) {
    options.active=qfalse; options.dragging=qfalse;
    // Key releases held by the panel remain swallowed until physically up.
}
static void OptionsOpen(void) {
    int i;
    if (options.active) return;
    OptionsBuildDefinitions(qtrue);
    // Release gameplay before claiming input; never replace a mod's catcher.
    Key_ClearStates();
    Con_Close();
    cl.mouseDx[0]=cl.mouseDx[1]=cl.mouseDy[0]=cl.mouseDy[1]=0;
    for (i=0;i<optionCount;++i) {
        const char *v=Cvar_PendingString(optionDefs[i].name);
        if (!*v) v=optionDefs[i].initial;
        Q_strncpyz(options.pending[i],v,OPTIONS_VALUE);
        Q_strncpyz(options.original[i],v,OPTIONS_VALUE);
    }
    options.active=qtrue; options.page=0; options.selected=0;
    options.mouseX=450; options.mouseY=70;
    Q_strncpyz(options.status,profileSafeMode ?
        "Safe mode: the shared profile will not be loaded or overwritten.":
        "Changes are staged. Apply saves them for every game and mod.",sizeof(options.status));
}
static void OptionsApply(void);
static void OptionsResetExposure(void) {
    int i;
    for (i=0;i<optionCount;++i)
        if (optionDefs[i].page==5) Q_strncpyz(options.pending[i],optionDefs[i].initial,OPTIONS_VALUE);
}
static void OptionsCommand(void) {
    if (Cmd_Argc()>1 && !Q_stricmp(Cmd_Argv(1),"close")) { OptionsClose(); return; }
    if (Cmd_Argc()>1 && !Q_stricmp(Cmd_Argv(1),"apply")) {
        if (options.active) OptionsApply();
        return;
    }
    OptionsOpen();
    if (Cmd_Argc()>1) {
        int i;
        for (i=0;i<ARRAY_LEN(pageNames);++i)
            if (!Q_stricmp(pageNames[i],Cmd_Argv(1))) { options.page=i; options.selected=0; options.dragging=qfalse; }
    }
}
static qboolean OptionEnabled(int i) {
    const char *name=optionDefs[i].name;
    qboolean vulkan=!Q_stricmp(options.pending[0],"vulkan");
    if (optionDefs[i].page==1 && !vulkan) return qfalse;
    if (optionDefs[i].page==4 && !vulkan) return qfalse;
    if (optionDefs[i].page==5 && !vulkan) return qfalse;
    if (optionDefs[i].page!=2) return qtrue;
    if (!vulkan) return qfalse;
    if (!strcmp(name,"r_dlssFrameGeneration")) return Cvar_VariableIntegerValue("r_dlssFrameGenerationAvailable")!=0;
    if (!strcmp(name,"r_dlssFrameGenerationMultiplier")) {
        int j;
        if (!Cvar_VariableIntegerValue("r_dlssFrameGenerationAvailable") ||
            Cvar_VariableIntegerValue("r_dlssFrameGenerationMaxMultiplier")<=2) return qfalse;
        for (j=0;j<optionCount;++j)
            if (!strcmp(optionDefs[j].name,"r_dlssFrameGeneration")) return atoi(options.pending[j])!=0;
        return qfalse;
    }
    if (!strcmp(name,"r_reflex")) return Cvar_VariableIntegerValue("r_reflexAvailable")!=0;
    if (!strcmp(name,"r_dlssRayReconstruction")) return Cvar_VariableIntegerValue("r_dlssRayReconstructionAvailable")!=0;
    if (!strncmp(name,"r_dlssNR",8) || !strcmp(name,"r_dlssNeuralRendering"))
        return Cvar_VariableIntegerValue("r_dlssNeuralRenderingAvailable")!=0;
    return Cvar_VariableIntegerValue("r_dlssAvailable")!=0;
}
static int OptionRow(int row) {
    int i;
    for (i=0;i<optionCount;++i) {
        int owner=effectOwner[i];
        if (owner>=0 && i!=effectHeader[owner] && !effectExpanded[owner]) continue;
        if (optionDefs[i].page==options.page && row--==0) return i;
    }
    return -1;
}
static int OptionRows(void) { int n=0; while (OptionRow(n)>=0) ++n; return n; }
static int OptionScroll(void) { return MAX(0,options.selected-OPTIONS_VISIBLE+1); }
static void OptionToggleGroup(int i) {
    int row,owner;
    if (i<0 || (owner=effectOwner[i])<0 || i!=effectHeader[owner]) return;
    effectExpanded[owner]=!effectExpanded[owner];
    options.dragging=qfalse;
    // Retain the header selection when the visible row mapping changes.
    for (row=0;OptionRow(row)>=0;++row)
        if (OptionRow(row)==i) { options.selected=row; break; }
}
static float OptionFraction(const optionDef_t *d,float value) {
    value=Com_Clamp(d->minimum,d->maximum,value);
    if (d->page==5) {
        // Exposure spans several orders of magnitude. A zero-safe log scale
        // retains fine control near normal values without hiding the full range.
        float lo=log1pf(d->minimum/d->step),hi=log1pf(d->maximum/d->step);
        return (log1pf(value/d->step)-lo)/(hi-lo);
    }
    return (value-d->minimum)/(d->maximum-d->minimum);
}
static void OptionChange(int i, int direction, float fraction) {
    const optionDef_t *d;
    if (i<0) return;
    d=&optionDefs[i];
    if (!OptionEnabled(i)) {
        // Hardware support can disappear when changing renderer/adapter. Allow
        // disabling a remembered feature, but never enabling an unsupported one.
        if (d->choices && OptionIndex(d->choices,"0")>=0)
            Q_strncpyz(options.pending[i],"0",OPTIONS_VALUE);
        return;
    }
    if (d->choices) {
        char token[OPTIONS_VALUE];
        int n=0,index=OptionIndex(d->choices,options.pending[i]);
        while (OptionToken(d->choices,n,token,sizeof(token))) ++n;
        index=(index<0 ? 0:(index+direction+n)%n);
        OptionToken(d->choices,index,options.pending[i],OPTIONS_VALUE);
    } else {
        float v=atof(options.pending[i]);
        if (fraction>=0) {
            fraction=Com_Clamp(0,1,fraction);
            if (d->page==5) {
                float lo=log1pf(d->minimum/d->step),hi=log1pf(d->maximum/d->step);
                v=d->step*expm1f(lo+fraction*(hi-lo));
            } else v=d->minimum+fraction*(d->maximum-d->minimum);
        }
        else v+=direction*d->step;
        v=Com_Clamp(d->minimum,d->maximum,roundf(v/d->step)*d->step);
        if (!strcmp(d->name,"r_dlssFrameGenerationMultiplier"))
            v=MIN(v,Com_Clamp(2,6,Cvar_VariableIntegerValue("r_dlssFrameGenerationMaxMultiplier")));
        Com_sprintf(options.pending[i],OPTIONS_VALUE,d->page==5 ? "%.7g":"%.4g",v);
    }
}
static void OptionsApply(void) {
    int i;
    qboolean restart=qfalse, saved;
    for (i=0;i<optionCount;++i)
        if (strcmp(options.pending[i],options.original[i]) && OptionValid(i,options.pending[i])) {
            if (optionDefs[i].restart) Cvar_SetLatched(optionDefs[i].name,options.pending[i]);
            else Cvar_Set(optionDefs[i].name,options.pending[i]);
            if (optionDefs[i].restart) restart=qtrue;
        }
    saved=CL_OptionsSaveGlobal();
    OptionsClose();
    if (restart) CL_RequestVideoRestart(0);
    Com_Printf("VQ3 options: settings applied%s%s\n",profileSafeMode ? " (safe mode; profile untouched)":
        saved ? " and shared":" (shared profile could not be saved)",
        restart ? "; deferred video restart":"");
}

void CL_OptionsClearInput(void) {
    memset(options.swallowing,0,sizeof(options.swallowing));
    options.dragging=qfalse;
    memset(mouseRemainder,0,sizeof(mouseRemainder));
}

qboolean CL_OptionsKey(int key, qboolean down) {
    if (key<0 || key>=MAX_KEYS) return qfalse;
    if (!down && options.swallowing[key]) {
        options.swallowing[key]=qfalse;
        if (key==K_MOUSE1) options.dragging=qfalse;
        return qtrue;
    }
    if (down && key==K_F10 && (Key_IsDown(K_SHIFT) || options.swallowing[K_SHIFT])) {
        if (!options.swallowing[key]) { if (options.active) OptionsClose(); else OptionsOpen(); }
        options.swallowing[key]=qtrue; return qtrue;
    }
    if (!options.active) return qfalse;
    options.swallowing[key]=down;
    if (!down) { if (key==K_MOUSE1) options.dragging=qfalse; return qtrue; }
    if (key==K_ESCAPE) { OptionsClose(); return qtrue; }
    if (key==K_ENTER || key==K_KP_ENTER) { OptionsApply(); return qtrue; }
    if (key!=K_MOUSE1 && key!=K_SHIFT) options.dragging=qfalse;
    if (key==K_SPACE) OptionToggleGroup(OptionRow(options.selected));
    if (key==K_TAB) { options.page=(options.page+((Key_IsDown(K_SHIFT)||options.swallowing[K_SHIFT])?ARRAY_LEN(pageNames)-1:1))%ARRAY_LEN(pageNames); options.selected=0; }
    if (key==K_UPARROW || key==K_MWHEELUP) options.selected=(options.selected+OptionRows()-1)%OptionRows();
    if (key==K_DOWNARROW || key==K_MWHEELDOWN) options.selected=(options.selected+1)%OptionRows();
    if (key==K_LEFTARROW || key==K_RIGHTARROW)
        OptionChange(OptionRow(options.selected),key==K_RIGHTARROW ? 1:-1,-1);
    if (key==K_MOUSE1) {
        float x=options.mouseX,y=options.mouseY;
        int row=(int)((y-140)/40)+OptionScroll();
        options.dragging=qfalse;
        if (y>=89 && y<123 && x>=26 && x<874) { options.page=MIN(ARRAY_LEN(pageNames)-1,(int)((x-26)/(848.f/ARRAY_LEN(pageNames)))); options.selected=0; }
        else if (options.page==5 && y>=580 && y<620 && x>=26 && x<270) OptionsResetExposure();
        else if (y>=580 && y<620 && x>=510 && x<680) OptionsApply();
        else if (y>=580 && y<620 && x>=700 && x<874) OptionsClose();
        else if (y>=140 && y<540 && row<OptionRows() && x>=32 && x<860) {
            int i=OptionRow(row); options.selected=row;
            if (x<400) OptionToggleGroup(i);
            else if (optionDefs[i].choices) OptionChange(i,x<470 ? -1:1,-1);
            else {
                options.dragging=qtrue;
                OptionChange(i,0,(x-430)/310);
            }
        }
    }
    return qtrue;
}
qboolean CL_OptionsMouse(int dx, int dy) {
    if (!options.active) return qfalse;
    options.mouseX=Com_Clamp(0,900,options.mouseX+dx/MAX(options.scale,.1f));
    options.mouseY=Com_Clamp(0,655,options.mouseY+dy/MAX(options.scale,.1f));
    if (options.dragging) OptionChange(OptionRow(options.selected),0,(options.mouseX-430)/310);
    return qtrue;
}
static void OptionsRect(float x,float y,float w,float h,const float *color) {
    re.SetColor(color);
    re.DrawStretchPic(options.left+x*options.scale,options.top+y*options.scale,
        w*options.scale,h*options.scale,0,0,0,0,cls.whiteShader);
}
static void OptionsText(float x,float y,float size,const char *text,const float *color) {
    re.SetColor(color);
    for (;*text;++text,x+=size*.62f) {
        unsigned ch=(unsigned char)*text;
        float s=(ch&15)/16.0f,t=(ch>>4)/16.0f;
        if (ch!=' ') re.DrawStretchPic(options.left+x*options.scale,options.top+y*options.scale,
            size*.62f*options.scale,size*options.scale,s,t,s+.0625f,t+.0625f,cls.charSetShader);
    }
}
void CL_OptionsDraw(void) {
    static const vec4_t bg={.018f,.027f,.045f,.98f},stripe={.08f,.12f,.19f,1},white={.9f,.94f,1,1},
        muted={.5f,.6f,.72f,1},accent={.22f,.72f,.85f,1},shade={0,0,0,.7f};
    int row,i;
    if (!options.active) {
        // A universal, unobtrusive hint on any mod menu; no mod assets needed.
        if ((Key_GetCatcher()&KEYCATCH_UI) && !(Key_GetCatcher()&KEYCATCH_CONSOLE)) {
            options.scale=MIN(cls.glconfig.vidWidth/960.f,cls.glconfig.vidHeight/720.f);
            options.left=16; options.top=cls.glconfig.vidHeight-24*options.scale;
            OptionsText(0,0,14,"VQ3 OPTIONS: SHIFT+F10",muted);
            re.SetColor(NULL);
        }
        return;
    }
    options.scale=MIN(cls.glconfig.vidWidth/960.f,cls.glconfig.vidHeight/720.f);
    options.left=(cls.glconfig.vidWidth-900*options.scale)*.5f;
    options.top=(cls.glconfig.vidHeight-655*options.scale)*.5f;
    re.SetColor(shade); re.DrawStretchPic(0,0,cls.glconfig.vidWidth,cls.glconfig.vidHeight,0,0,0,0,cls.whiteShader);
    OptionsRect(0,0,900,655,bg);
    OptionsText(26,20,25,"VQ3 EVOLUTION / OPTIONS",white);
    OptionsText(26,58,14,"Engine settings - available in every game and mod",muted);
    for (i=0;i<ARRAY_LEN(pageNames);++i) {
        float tab=848.f/ARRAY_LEN(pageNames);
        OptionsRect(26+i*tab,89,tab-4,34,i==options.page ? stripe:bg);
        OptionsText(40+i*tab,98,16,pageNames[i],i==options.page ? accent:muted);
    }
    for (row=OptionScroll();row<OptionScroll()+OPTIONS_VISIBLE && (i=OptionRow(row))>=0;++row) {
        const optionDef_t *d=&optionDefs[i];
        int owner=effectOwner[i];
        float y=140+(row-OptionScroll())*40;
        const float *color=OptionEnabled(i) ? white:muted;
        char label[OPTIONS_VALUE];
        if (row==options.selected) OptionsRect(20,y-3,860,36,stripe);
        // Bound package labels to the left column; long author labels must not
        // overlap enable switches or slider tracks.
        if (owner>=0) {
            if (i==effectHeader[owner]) OptionsText(32,y+8,16,effectExpanded[owner] ? "[-]":"[+]",accent);
            Com_sprintf(label,sizeof(label),"%.32s",d->label);
            OptionsText(72,y+8,16,label,color);
        } else OptionsText(32,y+8,16,d->label,color);
        if (d->choices) {
            int index=OptionIndex(d->choices,options.pending[i]);
            if (!OptionToken(d->labels,index,label,sizeof(label)))
                Com_sprintf(label,sizeof(label),"Current: %s",options.pending[i]);
            if (!strcmp(d->name,"r_rayTracing") && !strcmp(options.pending[i],"1"))
                Q_strncpyz(label,"Software (WIP; console)",sizeof(label));
            OptionsText(430,y+8,16,"<",muted); OptionsText(463,y+8,16,label,color); OptionsText(840,y+8,16,">",muted);
        } else {
            float fraction=OptionFraction(d,atof(options.pending[i]));
            OptionsRect(430,y+15,310,4,muted); OptionsRect(430+fraction*310-3,y+9,6,16,accent);
            Com_sprintf(label,sizeof(label),"%s%s",options.pending[i],
                !strcmp(d->name,"r_dlssFrameGenerationMultiplier") ? "x":
                !strcmp(d->name,"cg_fov") ? " deg":"");
            OptionsText(765,y+8,16,label,color);
        }
    }
    if (options.page==3) {
        OptionsText(32,343,14,moduleScales[1] ? "Menu layout: handled by this module":"Menu layout: engine compatibility scaling",muted);
        OptionsText(32,369,14,moduleScales[0] ? "HUD layout: handled by this module":"HUD layout: centered compatibility scaling",muted);
        OptionsText(32,408,14,"Automatic avoids scaling twice. Disable for custom 3D interfaces.",muted);
    }
    if (options.page==0) {
        int selected=OptionRow(options.selected);
        if (selected>=0 && !strcmp(optionDefs[selected].name,"cg_fov"))
            OptionsText(26,563,12,"4:3 base degrees; widescreen expands the view. Mods and servers may restrict FOV.",muted);
        else if (selected>=0 && !strcmp(optionDefs[selected].name,"r_swapInterval"))
            OptionsText(26,563,12,"VSync needs a renderer restart. Vulkan Frame Generation overrides it while active.",muted);
    }
    if (options.page==2)
        OptionsText(26,563,12,"Dimmed controls are unavailable on the active renderer / hardware.",muted);
    if (options.page==4) {
        const char *status=effectCount ? "Click a shader name or press SPACE to show/hide its settings.":
            "No local effects found. Install packages in postfx/; use postfx_reload.";
        int selected=OptionRow(options.selected);
        for (i=0;i<effectCount && selected>=0;++i) {
            char prefix[40]; Com_sprintf(prefix,sizeof(prefix),"r_fx_%s_",effectInfo[i].id);
            if (!strncmp(optionDefs[selected].name,prefix,strlen(prefix))) { status=effectInfo[i].status; break; }
        }
        OptionsText(26,563,12,status,muted);
    }
    if (options.page==5) {
        OptionsText(32,408,14,"Base exposure also sets the baseline for automatic exposure.",muted);
        OptionsText(32,434,14,"Smaller adaptation time = faster response. Auto target 0.18 = neutral.",muted);
        OptionsText(32,460,14,"Sliders use a logarithmic scale; arrow keys make fine adjustments.",muted);
        OptionsText(26,563,12,"Path-traced scene only; HUD and menus are unaffected. Apply does not restart the renderer.",muted);
    }
    OptionsText(26,547,13,options.status,muted);
    if (options.page==5) {
        OptionsRect(26,580,244,40,stripe); OptionsText(42,592,16,"RESET EXPOSURE",white);
    }
    OptionsRect(510,580,170,40,stripe); OptionsText(535,592,16,"APPLY",accent);
    OptionsRect(700,580,174,40,stripe); OptionsText(725,592,16,"CANCEL",white);
    OptionsText(26,631,12,options.page==4 ?
        "TAB: category   SPACE: fold shader   ARROWS: adjust   ENTER: apply   ESC: cancel":
        "TAB: category   ARROWS: adjust   ENTER: apply   ESC: cancel   Online games do not pause",muted);
    OptionsRect(options.mouseX,options.mouseY,3,14,white);
    OptionsRect(options.mouseX,options.mouseY,10,3,white);
    re.SetColor(NULL);
}

// Config has loaded, but the renderer has not registered package defaults yet.
// Import only missing preferences; an old config must not overwrite a newer
// explicit effect choice. The post-effect master remains under user control.
static void OptionsMigrateBloom(void) {
    static const struct { const char *oldName,*newName; float maximum; } mappings[]={
        {"r_postBloom","r_fx_bloom_enabled",1},
        {"r_postBloomStrength","r_fx_bloom_strength",4},
        {"r_postBloomThreshold","r_fx_bloom_threshold",1}
    };
    for (int i=0;i<ARRAY_LEN(mappings);++i) {
        const char *previous; float value;
        if (*Cvar_VariableString(mappings[i].newName)) continue;
        previous=Cvar_VariableString(mappings[i].oldName);
        if (PFX_Number(previous,&value) && value>=0 && value<=mappings[i].maximum) {
            Cvar_Get(mappings[i].newName,previous,CVAR_ARCHIVE);
            Cvar_Set(mappings[i].newName,previous); // Saved legacy preference wins over the release default.
            Com_Printf("PostFX: migrated %s to %s\n",mappings[i].oldName,mappings[i].newName);
        }
    }
}
void CL_OptionsInit(void) {
    OptionsMigrateBloom();
    OptionsBuildDefinitions(qfalse);
    // Register shared compatibility/weapon controls even in the main menu.
    // Other renderer defaults remain renderer-owned.
    Cvar_Get("cg_fov","90",CVAR_ARCHIVE);
    Cvar_Get("r_muzzleFlashBrightness","1",CVAR_ARCHIVE);
    Cvar_Get("r_muzzleFlashLightScale","1",CVAR_ARCHIVE);
    Cvar_Get("r_rocketBrightness","1",CVAR_ARCHIVE);
    Cvar_Get("r_rocketLightScale","1",CVAR_ARCHIVE);
    Cvar_Get("r_rocketExplosionLightScale","1",CVAR_ARCHIVE);
    Cvar_Get("r_lightningGunLightScale","1",CVAR_ARCHIVE);
    Cvar_Get("r_rocketExplosionLightScale","1",CVAR_ARCHIVE);
    Cvar_Get("r_lightningGunLightScale","1",CVAR_ARCHIVE);
    legacyScale[1]=Cvar_Get("ui_scale","1",CVAR_ARCHIVE);
    legacyScale[0]=Cvar_Get("cg_hudScale","1",CVAR_ARCHIVE);
    legacyMode[1]=Cvar_Get("cl_legacyUIScale","1",CVAR_ARCHIVE);
    legacyMode[0]=Cvar_Get("cl_legacyHUDScale","1",CVAR_ARCHIVE);
    memset(&options,0,sizeof(options));
    Cmd_AddCommand("vq3e_options",OptionsCommand);
}
void CL_OptionsShutdown(void) { OptionsClose(); Cmd_RemoveCommand("vq3e_options"); }

// Legacy draw-call adaptation; native/module-aware layouts keep their own
// edge anchoring. Full-screen effects and world rendering are never shrunk.
void CL_OptionsResetModule(qboolean ui) {
    moduleScales[ui]=qfalse;
    mouseRemainder[ui][0]=mouseRemainder[ui][1]=0;
}
void CL_OptionsRegisterScale(qboolean ui,const char *name) {
    if (!Q_stricmp(name,ui ? "ui_scale":"cg_hudScale")) moduleScales[ui]=qtrue;
}
static float OptionsLegacyScale(qboolean ui) {
    int mode=legacyMode[ui] ? legacyMode[ui]->integer:0;
    if (!mode || (mode!=2 && moduleScales[ui])) return 1;
    return Com_Clamp(.5f,1.5f,legacyScale[ui]->value);
}
void CL_OptionsScaleRect(qboolean ui,float *x,float *y,float *w,float *h) {
    float scale=OptionsLegacyScale(ui),width=cls.glconfig.vidWidth,height=cls.glconfig.vidHeight;
    if (scale==1 || (MIN(*x,*x+*w)<=0 && MIN(*y,*y+*h)<=0 &&
        MAX(*x,*x+*w)>=width-1 && MAX(*y,*y+*h)>=height-1)) return;
    *x=width*.5f+(*x-width*.5f)*scale; *y=height*.5f+(*y-height*.5f)*scale;
    *w*=scale; *h*=scale;
}
void CL_OptionsScaleMouse(qboolean ui,int *dx,int *dy) {
    // Keep the accumulated input in double precision: release fast-math's
    // approximate packed float reciprocal can otherwise lose a whole pixel.
    double scale=OptionsLegacyScale(ui);
    mouseRemainder[ui][0]+=*dx/scale; mouseRemainder[ui][1]+=*dy/scale;
    *dx=(int)mouseRemainder[ui][0]; *dy=(int)mouseRemainder[ui][1];
    mouseRemainder[ui][0]-=*dx; mouseRemainder[ui][1]-=*dy;
}
void CL_OptionsStretchPic(qboolean ui,float x,float y,float w,float h,
    float s1,float t1,float s2,float t2,qhandle_t shader) {
    CL_OptionsScaleRect(ui,&x,&y,&w,&h);
    re.DrawStretchPic(x,y,w,h,s1,t1,s2,t2,shader);
}
void CL_OptionsRenderScene(qboolean ui,const refdef_t *scene) {
    refdef_t scaled;
    float x,y,w,h;
    if (!(scene->rdflags&RDF_NOWORLDMODEL)) { re.RenderScene(scene); return; }
    scaled=*scene; x=scene->x; y=scene->y; w=scene->width; h=scene->height;
    CL_OptionsScaleRect(ui,&x,&y,&w,&h);
    scaled.x=(int)x; scaled.y=(int)y; scaled.width=MAX(1,(int)w); scaled.height=MAX(1,(int)h);
    re.RenderScene(&scaled);
}
