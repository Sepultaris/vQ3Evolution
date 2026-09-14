#ifndef CL_POSTFX_H
#define CL_POSTFX_H
#include "../renderercommon/postfx.h"
int CL_PostFXList(char names[PFX_MAX_EFFECTS][PFX_ID]);
long CL_PostFXRead(const char *file, void **data);
void CL_PostFXFree(void *data);
#endif
