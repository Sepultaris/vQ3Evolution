/* Compile the actual engine panel/profile/compatibility implementation with
 * mocked filesystem, input, renderer and cvars. No real settings are touched. */
#include "../code/client/cl_options.c"
#include <assert.h>
clientActive_t cl;
clientStatic_t cls;
clientConnection_t clc;
refexport_t re;
static cvar_t running={.integer=1};
cvar_t *com_cl_running=&running;
static cvar_t vars[128];
static char names[128][64],values[128][128],latched[128][128];
static int varCount,sets,restarts,clears,argcValue;
static const char *argvValue[4];
static qboolean shift, replaceFails, writeFails;
static char profile[16384],written[16384];
static float rect[4];
static refdef_t drawnScene;
static int Find(const char *name) { int i; for(i=0;i<varCount;++i) if(!strcmp(names[i],name)) return i; return -1; }
static int Def(const char *name) { int i; for(i=0;i<ARRAY_LEN(optionDefs);++i) if(!strcmp(optionDefs[i].name,name)) return i; abort(); }
cvar_t *Cvar_Get(const char *name,const char *value,int flags) {
    int i=Find(name);
    if(i<0) { i=varCount++; assert(i<128); strcpy(names[i],name); strcpy(values[i],value);
        vars[i].name=names[i]; vars[i].string=values[i]; vars[i].value=atof(value); vars[i].integer=atoi(value); }
    vars[i].flags|=flags; return &vars[i];
}
void Cvar_Set(const char *name,const char *value) {
    cvar_t *c=Cvar_Get(name,value,0); int i=Find(name); ++sets;
    strcpy(values[i],value); c->latchedString=NULL; c->value=atof(value); c->integer=atoi(value);
}
void Cvar_SetLatched(const char *name,const char *value) {
    int i=Find(name);
    if(i<0 || !(vars[i].flags&CVAR_LATCH)) { Cvar_Set(name,value); return; }
    strcpy(latched[i],value); vars[i].latchedString=latched[i]; ++sets;
}
const char *Cvar_PendingString(const char *name) {
    int i=Find(name); return i<0 ? "":vars[i].latchedString ? vars[i].latchedString:vars[i].string;
}
float Cvar_VariableValue(const char *name) { int i=Find(name); return i<0 ? 0:vars[i].value; }
int Cvar_VariableIntegerValue(const char *name) { return (int)Cvar_VariableValue(name); }
qboolean Key_IsDown(int key) { return key==K_SHIFT && shift; }
void Key_ClearStates(void) { ++clears; shift=qfalse; CL_OptionsClearInput(); }
int Key_GetCatcher(void) { return KEYCATCH_UI; }
void Con_Close(void) {}
void CL_RequestVideoRestart(int delay) { assert(delay==0); ++restarts; }
void Cmd_AddCommand(const char *name,xcommand_t fn) { assert(!strcmp(name,"vq3e_options") && fn); }
void Cmd_RemoveCommand(const char *name) { assert(!strcmp(name,"vq3e_options")); }
int Cmd_Argc(void) { return argcValue; }
char *Cmd_Argv(int arg) { return (char *)(arg<argcValue ? argvValue[arg]:""); }
void QDECL Com_Printf(const char *format,...) { (void)format; }
void QDECL Com_Error(int code,const char *format,...) { (void)code; (void)format; abort(); }
long FS_SV_FOpenFileRead(const char *path,fileHandle_t *file) {
    assert(!strcmp(path,OPTIONS_FILE)); *file=*profile ? 1:0; return strlen(profile);
}
fileHandle_t FS_SV_FOpenFileWrite(const char *path) { assert(!strcmp(path,OPTIONS_FILE ".tmp")); *written=0; return 2; }
int FS_Read(void *buffer,int length,fileHandle_t file) { assert(file==1); memcpy(buffer,profile,length); return length; }
void FS_FCloseFile(fileHandle_t file) { assert(file==1 || file==2); }
void FS_ForceFlush(fileHandle_t file) { assert(file==2); }
int FS_Write(const void *data,int length,fileHandle_t file) {
    assert(file==2 && length<(int)sizeof(written));
    if (writeFails) return 0;
    memcpy(written,data,length); written[length]=0; return length;
}
void QDECL FS_Printf(fileHandle_t file,const char *format,...) {
    va_list args; int length=strlen(written); assert(file==2);
    va_start(args,format); vsnprintf(written+length,sizeof(written)-length,format,args); va_end(args);
}
qboolean FS_SV_ReplaceFile(const char *from,const char *to) {
    assert(!strcmp(from,OPTIONS_FILE ".tmp") && !strcmp(to,OPTIONS_FILE));
    if (replaceFails) return qfalse;
    strcpy(profile,written); return qtrue;
}
static void Draw(float x,float y,float w,float h,float a,float b,float c,float d,qhandle_t shader) {
    (void)a;(void)b;(void)c;(void)d;(void)shader; rect[0]=x;rect[1]=y;rect[2]=w;rect[3]=h;
}
static void DrawScene(const refdef_t *scene) { drawnScene=*scene; }
static void Color(const float *color) { (void)color; }
static void Close(float a,float b) { assert(fabsf(a-b)<.001f); }
int main(void) {
    int i,before;
    cls.glconfig.vidWidth=1920; cls.glconfig.vidHeight=1080;
    re.DrawStretchPic=Draw; re.RenderScene=DrawScene; re.SetColor=Color;
    CL_OptionsInit();
    assert(ARRAY_LEN(optionDefs)<=OPTIONS_MAX);
    for(i=0;i<ARRAY_LEN(optionDefs);++i) {
        assert(OptionValid(i,optionDefs[i].initial));
        Cvar_Get(optionDefs[i].name,optionDefs[i].initial,optionDefs[i].restart ? CVAR_LATCH:0);
        assert(!OptionValid(i,"nan") && !OptionValid(i,"inf") && !OptionValid(i,"1;quit"));
        assert(!OptionValid(i,"1e999") && !OptionValid(i,"../bad"));
    }
    before=sets; OptionsOpen(); assert(sets==before && clears==1 && options.active);
    options.page=1; i=Def("r_pathTracingSamples"); OptionChange(i,1,-1);
    assert(!strcmp(options.pending[i],"3") && Cvar_VariableIntegerValue("r_pathTracingSamples")==2);
    assert(CL_OptionsKey(K_ESCAPE,qtrue) && !options.active && sets==before);
    assert(CL_OptionsKey(K_ESCAPE,qfalse));
    OptionsOpen(); options.page=1; OptionChange(i,1,-1); OptionsApply();
    assert(Cvar_VariableIntegerValue("r_pathTracingSamples")==3 && restarts==0 && !options.active);
    assert(strstr(profile,"r_pathTracingSamples 3\n"));
    OptionsOpen(); i=Def("r_fullscreen"); OptionChange(i,1,-1); OptionsApply();
    assert(restarts==1 && Cvar_VariableIntegerValue("r_fullscreen")==0);
    assert(!strcmp(Cvar_PendingString("r_fullscreen"),"1") && strstr(profile,"r_fullscreen 1\n"));
    puts("PASS: open/cancel do not mutate; apply persists values; latched settings defer safely");
    Cvar_Set("r_pathTracingSamples","2"); CL_OptionsLoadGlobal(qfalse);
    assert(Cvar_VariableIntegerValue("r_pathTracingSamples")==3);
    strcpy(profile,"quit now\nbind w quit\nr_pathTracingSamples nan\nr_pathTracingExposure 1e999\nr_dlss 5\nui_scale .75\n");
    before=sets; CL_OptionsLoadGlobal(qfalse); assert(sets==before+2 && Find("quit")==-1 && Find("bind")==-1);
    assert(Cvar_VariableValue("ui_scale")==.75f && Cvar_VariableIntegerValue("r_dlss")==5);
    strcpy(profile,"r_dlss 0 extra\nr_pathTracingSamples\nui_scale 1 comment\nr_dlss 4");
    before=sets; CL_OptionsLoadGlobal(qfalse); assert(sets==before+1 && Cvar_VariableIntegerValue("r_dlss")==4);
    puts("PASS: game-independent profile restores settings; unknown commands, NaN, overflow and injection rejected");
    strcpy(profile,"r_muzzleFlashBrightness .25\nr_muzzleFlashLightScale 2\n");
    CL_OptionsLoadGlobal(qfalse);
    assert(Cvar_VariableValue("r_muzzleFlashBrightness")==.25f && Cvar_VariableValue("r_muzzleFlashLightScale")==2);
    assert(optionDefs[Def("r_muzzleFlashBrightness")].page==-1 && !optionDefs[Def("r_muzzleFlashBrightness")].restart);
    assert(optionDefs[Def("r_muzzleFlashLightScale")].page==-1 && !optionDefs[Def("r_muzzleFlashLightScale")].restart);
    assert(CL_OptionsSaveGlobal() && strstr(profile,"r_muzzleFlashBrightness .25\n") && strstr(profile,"r_muzzleFlashLightScale 2\n"));
    puts("PASS: independent live console-only muzzle controls persist through the shared profile");
    strcpy(profile,"r_rocketBrightness .25\nr_rocketLightScale .5\n");
    CL_OptionsLoadGlobal(qfalse);
    assert(Cvar_VariableValue("r_rocketBrightness")==.25f && Cvar_VariableValue("r_rocketLightScale")==.5f);
    assert(Cvar_VariableValue("r_muzzleFlashBrightness")==.25f && Cvar_VariableValue("r_muzzleFlashLightScale")==2);
    assert(optionDefs[Def("r_rocketBrightness")].page==-1 && !optionDefs[Def("r_rocketBrightness")].restart);
    assert(optionDefs[Def("r_rocketLightScale")].page==-1 && !optionDefs[Def("r_rocketLightScale")].restart);
    assert(CL_OptionsSaveGlobal() && strstr(profile,"r_rocketBrightness .25\n") && strstr(profile,"r_rocketLightScale .5\n"));
    puts("PASS: independent live console-only rocket controls persist without changing muzzle controls");
    strcpy(profile,"r_rocketExplosionLightScale .125\nr_lightningGunLightScale .5\n");
    CL_OptionsLoadGlobal(qfalse);
    assert(Cvar_VariableValue("r_rocketExplosionLightScale")==.125f && Cvar_VariableValue("r_lightningGunLightScale")==.5f);
    assert(Cvar_VariableValue("r_rocketLightScale")==.5f && Cvar_VariableValue("r_muzzleFlashLightScale")==2);
    assert(optionDefs[Def("r_rocketExplosionLightScale")].page==-1 && !optionDefs[Def("r_rocketExplosionLightScale")].restart);
    assert(optionDefs[Def("r_lightningGunLightScale")].page==-1 && !optionDefs[Def("r_lightningGunLightScale")].restart);
    assert(CL_OptionsSaveGlobal() && strstr(profile,"r_rocketExplosionLightScale .125\n") && strstr(profile,"r_lightningGunLightScale .5\n"));
    puts("PASS: explosion and lightning illumination controls persist independently of existing settings");
    strcpy(profile,"r_rayTracing 1\nr_dlssNeuralRendering 3\nr_dlssNRIntensity 1.37\nr_dlssFrameGenerationMultiplier 6\n");
    CL_OptionsLoadGlobal(qfalse);
    assert(!strcmp(optionDefs[Def("r_rayTracing")].choices,"0|2"));
    assert(optionDefs[Def("r_dlssNeuralRendering")].page==-1);
    assert(optionDefs[Def("r_dlssNRIntensity")].page==-1);
    assert(optionDefs[Def("r_dlssNRLocalToneStrength")].page==-1);
    assert(optionDefs[Def("r_dlssNRLocalStructureStrength")].page==-1);
    assert(optionDefs[Def("r_dlssNRSkinStructureStrength")].page==-1);
    OptionsOpen(); OptionChange(Def("r_pathTracingSamples"),1,-1); OptionsApply();
    assert(strstr(profile,"r_rayTracing 1\n") && strstr(profile,"r_dlssNeuralRendering 3\n"));
    assert(strstr(profile,"r_dlssNRIntensity 1.37\n") && strstr(profile,"r_dlssFrameGenerationMultiplier 6\n"));
    OptionsOpen(); i=Def("r_rayTracing");
    OptionChange(i,1,-1); assert(!strcmp(options.pending[i],"0"));
    OptionChange(i,1,-1); assert(!strcmp(options.pending[i],"2"));
    OptionChange(i,1,-1); assert(!strcmp(options.pending[i],"0")); OptionsClose();
    Cvar_Set("r_dlssFrameGenerationAvailable","1");
    Cvar_Set("r_dlssFrameGenerationMaxMultiplier","6");
    OptionsOpen(); i=Def("r_dlssFrameGenerationMultiplier"); assert(!OptionEnabled(i));
    OptionChange(Def("r_dlssFrameGeneration"),1,-1); assert(OptionEnabled(i));
    OptionChange(i,0,.375f); assert(!strcmp(options.pending[i],"4"));
    OptionChange(i,0,1); assert(!strcmp(options.pending[i],"6"));
    Cvar_Set("r_dlssFrameGenerationMaxMultiplier","4");
    OptionChange(i,0,1); assert(!strcmp(options.pending[i],"4"));
    Cvar_Set("r_dlssFrameGenerationMaxMultiplier","2"); assert(!OptionEnabled(i));
    OptionsClose(); assert(Cvar_VariableIntegerValue("r_dlssFrameGeneration")==0);
    assert(!OptionValid(i,"1") && !OptionValid(i,"7") && !OptionValid(i,"2.5"));
    puts("PASS: WIP settings persist but are not selectable; lighting maps 0/2; integer 2x-6x slider respects capabilities, staging and cancel");
    OptionsOpen(); i=Def("r_dlss"); OptionChange(i,1,-1);
    assert(!strcmp(options.pending[i],"0")); OptionChange(i,1,-1);
    assert(!strcmp(options.pending[i],"0")); OptionsClose();
    puts("PASS: unavailable hardware features can be disabled but not enabled");

    OptionsOpen(); assert(CL_OptionsKey('w',qtrue)); assert(CL_OptionsKey(K_MOUSE1,qtrue));
    options.dragging=qtrue;
    assert(CL_OptionsKey(K_MOUSE1,qfalse) && !options.dragging);
    CL_OptionsKey(K_MOUSE1,qtrue);
    OptionsClose(); assert(CL_OptionsKey('w',qfalse)); assert(CL_OptionsKey(K_MOUSE1,qfalse));
    assert(!CL_OptionsKey('w',qtrue));
    shift=qtrue; assert(CL_OptionsKey(K_F10,qtrue) && options.active);
    assert(CL_OptionsKey(K_F10,qfalse));
    CL_OptionsKey(K_SHIFT,qtrue); CL_OptionsKey(K_TAB,qtrue); assert(options.page==3);
    CL_OptionsKey(K_TAB,qfalse); CL_OptionsKey(K_F10,qtrue); assert(!options.active);
    CL_OptionsKey(K_SHIFT,qfalse); CL_OptionsKey(K_F10,qfalse);
    OptionsOpen(); CL_OptionsKey(K_SHIFT,qtrue); options.dragging=qtrue;
    Key_ClearStates(); assert(!options.swallowing[K_SHIFT] && !options.dragging); OptionsClose();
    puts("PASS: panel owns keyboard/mouse pairs without leaking releases; global chord and reverse tabs work");

    Cvar_Set("ui_scale",".5"); Cvar_Set("cg_hudScale",".75");
    CL_OptionsResetModule(qtrue); CL_OptionsResetModule(qfalse);
    CL_OptionsStretchPic(qtrue,100,100,200,80,0,0,1,1,1);
    Close(rect[0],530); Close(rect[1],320); Close(rect[2],100); Close(rect[3],40);
    CL_OptionsStretchPic(qtrue,0,0,1920,1080,0,0,1,1,1);
    Close(rect[0],0); Close(rect[1],0); Close(rect[2],1920); Close(rect[3],1080);
    CL_OptionsStretchPic(qtrue,1920,1080,-1920,-1080,0,0,1,1,1);
    Close(rect[0],1920); Close(rect[1],1080); Close(rect[2],-1920); Close(rect[3],-1080);
    { int dx=3,dy=-2; CL_OptionsScaleMouse(qtrue,&dx,&dy); assert(dx==6 && dy==-4); }
    CL_OptionsRegisterScale(qtrue,"ui_scale");
    CL_OptionsStretchPic(qtrue,100,100,200,80,0,0,1,1,1); Close(rect[0],100); Close(rect[2],200);
    CL_OptionsStretchPic(qfalse,100,100,200,80,0,0,1,1,1); Close(rect[2],150);
    CL_OptionsRegisterScale(qfalse,"cg_hudScale");
    CL_OptionsStretchPic(qfalse,100,100,200,80,0,0,1,1,1); Close(rect[2],200);
    CL_OptionsResetModule(qtrue); Cvar_Set("cl_legacyUIScale","0");
    CL_OptionsStretchPic(qtrue,100,100,200,80,0,0,1,1,1); Close(rect[2],200);
    Cvar_Set("cl_legacyUIScale","1");
    { refdef_t scene={.x=100,.y=100,.width=200,.height=80};
      CL_OptionsRenderScene(qtrue,&scene); assert(drawnScene.width==200);
      scene.rdflags=RDF_NOWORLDMODEL; CL_OptionsRenderScene(qtrue,&scene);
      assert(drawnScene.width==100 && drawnScene.height==40 && scene.width==200); }
    puts("PASS: legacy UI/HUD scale, inverse mouse input, module ownership, opt-out, fullscreen effects and world-view exclusion");
    OptionsOpen();
    for(i=0;i<4;++i) { options.page=i; CL_OptionsDraw(); assert(OptionRows()<=10); }
    CL_OptionsShutdown(); assert(!options.active);
    puts("PASS: all engine-owned categories draw without VM calls and shutdown closes input ownership");
    strcpy(profile,"ui_scale .75\n"); replaceFails=qtrue;
    assert(!CL_OptionsSaveGlobal() && !strcmp(profile,"ui_scale .75\n")); replaceFails=qfalse;
    writeFails=qtrue;
    assert(!CL_OptionsSaveGlobal() && !strcmp(profile,"ui_scale .75\n")); writeFails=qfalse;
    assert(CL_OptionsSaveGlobal() && strstr(profile,"cl_renderer vulkan\n"));
    puts("PASS: failed writes/replacement preserve the old profile and report failure; successful replacement updates it");
    strcpy(profile,"ui_scale .5\n"); before=sets;
    CL_OptionsLoadGlobal(qtrue); CL_OptionsSaveGlobal(); CL_OptionsLoadGlobal(qfalse);
    assert(sets==before && !strcmp(profile,"ui_scale .5\n"));
    puts("PASS: safe mode neither loads nor overwrites the shared profile, including later mod switches");
    return 0;
}
