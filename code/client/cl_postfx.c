/* Only local, global postfx folders; never downloaded mod/PK3 shader code. */
#include "client.h"
#include "cl_postfx.h"
#include "../renderercommon/postfx_parse.h"

static int PFX_Compare(const void *a,const void *b) { return strcmp(a,b); }
static int PFX_ComparePointers(const void *a,const void *b) { return strcmp(*(const char *const *)a,*(const char *const *)b); }
int CL_PostFXList(char names[PFX_MAX_EFFECTS][PFX_ID]) {
    const char *roots[]={"fs_homepath","fs_basepath"};
    int count=0,r;
    for (r=0;r<2;++r) {
        int n=0,i; char path[MAX_OSPATH]; char **list;
        Com_sprintf(path,sizeof(path),"%s/postfx",Cvar_VariableString(roots[r]));
        list=Sys_ListFiles(path,".effect",NULL,&n,qfalse);
        if (!list) continue;
        qsort(list,n,sizeof(*list),PFX_ComparePointers);
        for (i=0;i<n && count<PFX_MAX_EFFECTS;++i) {
            char id[PFX_ID]; size_t len=strlen(list[i]); int j;
            if (len<=7 || len-7>=sizeof(id) || strcmp(list[i]+len-7,".effect")) continue;
            memcpy(id,list[i],len-7); id[len-7]=0;
            if (!PFX_Id(id)) continue;
            for (j=0;j<count;++j) if (!strcmp(names[j],id)) break;
            if (j==count) Q_strncpyz(names[count++],id,PFX_ID);
        }
        Sys_FreeFileList(list);
    }
    qsort(names,count,PFX_ID,PFX_Compare);
    return count;
}
long CL_PostFXRead(const char *file, void **data) {
    fileHandle_t handle; long length; char path[MAX_QPATH+PFX_FILE];
    *data=NULL;
    if (!PFX_File(file)) return -1;
    Com_sprintf(path,sizeof(path),"postfx/%s",file);
    length=FS_SV_FOpenFileRead(path,&handle);
    if (!handle) return -1;
    long limit=PFX_TextureFile(file) ? 32*1024*1024 : 1024*1024;
    if (length<=0 || length>limit) { FS_FCloseFile(handle); return -1; }
    *data=malloc(length+1);
    if (!*data) { FS_FCloseFile(handle); return -1; }
    if (FS_Read(*data,length,handle)!=length) { free(*data); *data=NULL; length=-1; }
    else ((char *)*data)[length]=0;
    FS_FCloseFile(handle); return length;
}
void CL_PostFXFree(void *data) { free(data); }
