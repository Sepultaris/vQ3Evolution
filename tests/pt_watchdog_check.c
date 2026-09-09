/* Offline process tests of the actual diagnostic launcher's watchdog.
 * Children are copies of this tiny console fixture: no game/window/GPU. */
#define main capture_launcher_main
#include "../tools/pt-crash-capture.c"
#undef main
#include <assert.h>

static void check_watchdog(int cancel, int expired)
{
    char executable[32768],command[32800];
    STARTUPINFOA startup={0}; PROCESS_INFORMATION child={0};
    startup.cb=sizeof(startup);
    assert(GetModuleFileNameA(NULL,executable,sizeof(executable)));
    snprintf(command,sizeof(command),"\"%s\" --child",executable);
    assert(CreateProcessA(executable,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,
        NULL,NULL,&startup,&child));
    ULONGLONG started=GetTickCount64();
    Watchdog watchdog={child.hProcess,CreateEventA(NULL,TRUE,FALSE,NULL),
        expired ? started-1:started+200,0};
    assert(watchdog.cancel);
    HANDLE thread=CreateThread(NULL,0,watchdog_main,&watchdog,0,NULL);
    assert(thread);
    if (cancel) SetEvent(watchdog.cancel);
    // Simulate diagnostic work longer than the deadline on the calling thread.
    Sleep(600);
    assert(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0);
    if (cancel) {
        assert(!watchdog.fired);
        assert(WaitForSingleObject(child.hProcess,0)==WAIT_TIMEOUT);
        assert(TerminateProcess(child.hProcess,0));
    } else {
        DWORD code=0;
        assert(watchdog.fired);
        assert(WaitForSingleObject(child.hProcess,0)==WAIT_OBJECT_0);
        assert(GetExitCodeProcess(child.hProcess,&code) && code==124);
    }
    assert(WaitForSingleObject(child.hProcess,2000)==WAIT_OBJECT_0);
    CloseHandle(thread); CloseHandle(watchdog.cancel);
    CloseHandle(child.hThread); CloseHandle(child.hProcess);
}

int main(int argc,char **argv)
{
    if (argc==2 && !strcmp(argv[1],"--child")) { Sleep(10000); return 0; }
    check_watchdog(0,0);
    check_watchdog(1,0);
    check_watchdog(0,1);
    puts("PASS: owned-child deadline survives blocked diagnostics; cancellation and expired deadline");
    return 0;
}
