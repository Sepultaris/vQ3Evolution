#include "../code/qcommon/q_shared.h"
#include <assert.h>
#include <errno.h>
static unsigned opens, closes, live, peak;
static int failOpen;
static FILE *checkedOpen(const char *name,const char *mode) {
    FILE *file;
    ++opens;
    assert(!strcmp(name,"pt_push_debug.log") && !strcmp(mode,"ab"));
    if (failOpen) { errno=EMFILE; return NULL; }
    file=tmpfile(); assert(file);
    ++live; if (live>peak) peak=live;
    return file;
}
static int checkedClose(FILE *file) {
    assert(live==1);
    --live; ++closes;
    return fclose(file);
}
#define fopen checkedOpen
#define fclose checkedClose
#include "../code/renderer_vulkan/pt_push_debug.h"
#undef fopen
#undef fclose
int main(void) {
    float c[30]={0};
    unsigned i;
    for(i=0;i<10000;++i) pt_push_debug_record(c,2,2,123);
    assert(opens==1 && closes==1 && live==0 && peak==1);
    for(i=0;i<4096;++i) pt_push_debug_record(c,1+(i%2),1+(i%64),123);
    assert(opens==4097 && closes==4097 && live==0 && peak==1);
    failOpen=1;
    pt_push_debug_record(c,1,2,123);
    for(i=0;i<10000;++i) pt_push_debug_record(c,1+(i%2),1+(i%64),123);
    assert(opens==4098 && closes==4097 && live==0);
    puts("PASS: 10000 unchanged frames open once; 4096 changes close every stream; failed opens do not retry each frame");
    return 0;
}
