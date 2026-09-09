/* Engine-owned presentation settings. No UI/cgame VM ABI changes. */
#ifndef CL_OPTIONS_H
#define CL_OPTIONS_H
void CL_OptionsInit(void);
void CL_OptionsShutdown(void);
void CL_OptionsDraw(void);
qboolean CL_OptionsActive(void);
qboolean CL_OptionsKey(int key, qboolean down);
qboolean CL_OptionsMouse(int dx, int dy);
void CL_OptionsClearInput(void);
void CL_OptionsScaleRect(qboolean ui, float *x, float *y, float *w, float *h);
void CL_OptionsResetModule(qboolean ui);
void CL_OptionsRegisterScale(qboolean ui, const char *name);
void CL_OptionsScaleMouse(qboolean ui, int *dx, int *dy);
void CL_OptionsStretchPic(qboolean ui, float x, float y, float w, float h,
    float s1, float t1, float s2, float t2, qhandle_t shader);
void CL_OptionsRenderScene(qboolean ui, const refdef_t *scene);
#endif
