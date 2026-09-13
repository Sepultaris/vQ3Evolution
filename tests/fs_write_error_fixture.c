// Exercise the production diagnostic without writing user files or configs.
#include "../code/qcommon/q_shared.h"
#include <assert.h>
#include <errno.h>
static cvar_t home={.string="test-home"};
static cvar_t *fs_homepath=&home;
static char output[MAX_OSPATH+256], recorded[MAX_OSPATH+256], opened[MAX_OSPATH];
static int messages, opens, failLog;
static FILE *Sys_FOpen(const char *path,const char *mode) {
    assert(!strcmp(mode,"ab"));
    Q_strncpyz(opened,path,sizeof(opened));
    ++opens;
    return failLog ? NULL:tmpfile();
}
static char *FS_BuildOSPath(const char *base,const char *game,const char *path) {
    static char result[MAX_OSPATH];
    Com_sprintf(result,sizeof(result),"%s/%s/%s",base,game,path);
    return result;
}
static int CloseLog(FILE *file) {
    fflush(file); rewind(file);
    size_t length=fread(recorded,1,sizeof(recorded)-1,file);
    recorded[length]=0;
    return fclose(file);
}
void QDECL Com_Printf(const char *fmt,...);
#define fclose CloseLog
#include "../code/qcommon/fs_write_error.h"
#undef fclose
void QDECL Com_Printf(const char *fmt,...) {
    va_list args; va_start(args,fmt);
    vsnprintf(output,sizeof(output),fmt,args); va_end(args);
    ++messages;
    // Model logfile-open failure during Com_Printf: no recursive diagnostic.
    FS_ReportWriteError("recursive.log",EACCES,5);
}
void QDECL Com_Error(int code,const char *fmt,...) { abort(); }
int main(void) {
    FS_ReportWriteError("test-home/baseq3/q3config.cfg",EACCES,32);
    assert(messages==1 && opens==1);
    assert(strstr(output,"test-home/baseq3/q3config.cfg") && strstr(output,"OS error 32"));
    assert(!strcmp(output,recorded) && !strcmp(opened,"test-home/filesystem-write-errors.log"));
    failLog=1;
    FS_ReportWriteError("test-home/baseq3/flash-error.txt",ENOENT,3);
    assert(messages==2 && opens==2 && strstr(output,"flash-error.txt") && strstr(output,"OS error 3"));
    puts("PASS: original path/OS error recorded at home root; recursive and unavailable logging remain safe");
    return 0;
}
