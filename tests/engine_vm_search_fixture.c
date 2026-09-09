/* Exercise the actual local VM resolver with synthetic search paths. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef int qboolean;
enum { qfalse=0, qtrue=1, VMI_NATIVE=0, VMI_COMPILED=2 };
typedef struct { const char *path, *gamedir; } directory_t;
typedef struct { const char *pakGamename, *pakPathname; } pack_t;
typedef struct searchpath_s {
    struct searchpath_s *next;
    pack_t *pack;
    directory_t *dir;
    int hasVM, hasDLL;
} searchpath_t;
static searchpath_t *fs_searchpaths;
static int FS_FilenameCompare(const char *a,const char *b) { return strcmp(a,b); }
static void Q_strncpyz(char *out,const char *in,int size) { snprintf(out,size,"%s",in); }
static const char *FS_BuildOSPath(const char *path,const char *game,const char *dll) {
    static char result[256]; snprintf(result,sizeof(result),"%s/%s/%s",path,game,dll); return result;
}
static int FS_FileInPathExists(const char *path) {
    searchpath_t *s; char expected[256];
    for(s=fs_searchpaths;s;s=s->next) if(s->dir && s->hasDLL) {
        snprintf(expected,sizeof(expected),"%s/%s/ui.dll",s->dir->path,s->dir->gamedir);
        if(!strcmp(path,expected)) return 1;
    }
    return 0;
}
static int FS_FOpenFileReadDir(const char *name,searchpath_t *s,void *file,qboolean unique,qboolean unpure) {
    assert(!strcmp(name,"vm/ui.qvm") && !file && !unique && unpure); return s->hasVM;
}
#include "../code/qcommon/fs_vm_search.h"
int main(void) {
    directory_t modHome={"home","mod"},modBuild={"build","mod"},base={"build","baseq3"},ta={"build","missionpack"};
    pack_t modNew={"mod","build/mod"},modOld={"mod","build/mod"},basePack={"baseq3","build/baseq3"};
    // Deliberately interleave packs/directories as pure reordering can do.
    searchpath_t s[7]={ {0,&basePack,0,1,0}, {0,&modNew,0,1,0}, {0,&modOld,0,1,0},
        {0,0,&modHome,0,0}, {0,0,&modBuild,0,0}, {0,0,&base,0,1}, {0,0,&ta,0,1} };
    const char *games[3]={"mod","missionpack","baseq3"};
    void *cursor=NULL; char found[256]; int i;
    for(i=0;i<6;++i) s[i].next=&s[i+1]; fs_searchpaths=s;
    assert(FS_FindLocalVM(&cursor,found,sizeof(found),"ui.dll","vm/ui.qvm",games)==VMI_COMPILED && cursor==&s[1]);
    // Failed newest mod pack must not retry its stale sibling or loop.
    assert(FS_FindLocalVM(&cursor,found,sizeof(found),"ui.dll","vm/ui.qvm",games)==VMI_NATIVE && cursor==&s[6]);
    assert(FS_FindLocalVM(&cursor,found,sizeof(found),"ui.dll","vm/ui.qvm",games)==VMI_NATIVE && cursor==&s[5]);
    assert(FS_FindLocalVM(&cursor,found,sizeof(found),"ui.dll","vm/ui.qvm",games)==VMI_COMPILED && cursor==&s[0]);
    assert(FS_FindLocalVM(&cursor,found,sizeof(found),"ui.dll","vm/ui.qvm",games)==-1);
    puts("PASS: mod QVM beats fallback native modules; retries advance without old-pack fallback or loops");
    s[3].hasDLL=s[4].hasDLL=1; cursor=NULL;
    assert(FS_FindLocalVM(&cursor,found,sizeof(found),"ui.dll","vm/ui.qvm",games)==VMI_NATIVE && cursor==&s[3]);
    assert(FS_FindLocalVM(&cursor,found,sizeof(found),"ui.dll","vm/ui.qvm",games)==VMI_NATIVE && cursor==&s[4]);
    assert(FS_FindLocalVM(&cursor,found,sizeof(found),"ui.dll","vm/ui.qvm",games)==VMI_COMPILED && cursor==&s[1]);
    cursor=NULL;
    assert(FS_FindLocalVM(&cursor,found,sizeof(found),NULL,"vm/ui.qvm",games)==VMI_COMPILED && cursor==&s[1]);
    puts("PASS: source-built modules preferred only within their own mod; bytecode-only request excludes DLLs");
    games[0]=games[1]=games[2]="baseq3"; cursor=NULL;
    assert(FS_FindLocalVM(&cursor,found,sizeof(found),"ui.dll","vm/ui.qvm",games)==VMI_NATIVE && cursor==&s[5]);
    assert(FS_FindLocalVM(&cursor,found,sizeof(found),"ui.dll","vm/ui.qvm",games)==VMI_COMPILED && cursor==&s[0]);
    assert(FS_FindLocalVM(&cursor,found,sizeof(found),"ui.dll","vm/ui.qvm",games)==-1);
    puts("PASS: duplicate base-game tiers are visited only once");
    return 0;
}
