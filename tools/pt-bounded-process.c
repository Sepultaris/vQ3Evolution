/* Profiling target wrapper: owns ONLY the process it creates and its children.
 * A Windows job closes them if this wrapper is killed/crashes. The game deadline
 * is independent of capture/export code in Nsight. No attaching to user apps. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

typedef struct { DWORD pid; HWND window; } ForegroundTarget;
static BOOL CALLBACK find_game_window(HWND window, LPARAM data)
{
    ForegroundTarget *target = (ForegroundTarget *)data;
    DWORD pid = 0; wchar_t name[64];
    GetWindowThreadProcessId(window, &pid);
    if (pid != target->pid || !IsWindowVisible(window)) return TRUE;
    if (!GetClassNameW(window, name, 64) || wcscmp(name, L"SDL_app")) return TRUE;
    target->window = window;
    return FALSE;
}

static DWORD bounded_process(const wchar_t *exe, const wchar_t *directory,
    const wchar_t *arguments, DWORD timeout_ms, DWORD flags)
{
    size_t length = wcslen(exe)+wcslen(arguments)+4;
    if (!timeout_ms || timeout_ms>120000 || length>32767) return 2;
    wchar_t *command = calloc(length,sizeof(wchar_t));
    if (!command) return 2;
    _snwprintf(command,length,L"\"%ls\" %ls",exe,arguments);
    HANDLE job = CreateJobObjectW(NULL,NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) {
        if (job) CloseHandle(job);
        free(command); return 3;
    }
    STARTUPINFOEXW startup = {0}; PROCESS_INFORMATION process = {0};
    HANDLE inherited[3] = {0}; SIZE_T attributes_size=0;
    startup.StartupInfo.cb = sizeof(startup);
    /* Forward tool diagnostics using ONLY these three explicitly duplicated
     * standard handles. Do not inherit the job handle or unrelated handles. */
    SECURITY_ATTRIBUTES security = {sizeof(security),NULL,TRUE};
    inherited[0]=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,NULL);
    HANDLE current=GetCurrentProcess();
    BOOL streams=inherited[0]!=INVALID_HANDLE_VALUE &&
        DuplicateHandle(current,GetStdHandle(STD_OUTPUT_HANDLE),current,&inherited[1],0,TRUE,DUPLICATE_SAME_ACCESS) &&
        DuplicateHandle(current,GetStdHandle(STD_ERROR_HANDLE),current,&inherited[2],0,TRUE,DUPLICATE_SAME_ACCESS);
    InitializeProcThreadAttributeList(NULL,1,0,&attributes_size);
    startup.lpAttributeList=malloc(attributes_size);
    BOOL initialized=startup.lpAttributeList && InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&attributes_size);
    BOOL configured=streams && initialized && UpdateProcThreadAttribute(startup.lpAttributeList,0,
        PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),NULL,NULL);
    startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput=inherited[0];
    startup.StartupInfo.hStdOutput=inherited[1];
    startup.StartupInfo.hStdError=inherited[2];
    BOOL created=configured && CreateProcessW(exe,command,NULL,NULL,TRUE,
        CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT|flags,NULL,directory,&startup.StartupInfo,&process);
    DWORD create_error=GetLastError();
    for(unsigned i=0;i<3;++i) if(inherited[i] && inherited[i]!=INVALID_HANDLE_VALUE) CloseHandle(inherited[i]);
    if(initialized) DeleteProcThreadAttributeList(startup.lpAttributeList);
    free(startup.lpAttributeList);
    if (!created) {
        fprintf(stderr,"VQ3E_BOUNDED create_failed=%lu\n",create_error);
        CloseHandle(job); free(command); return 4;
    }
    free(command);
    if (!AssignProcessToJobObject(job,process.hProcess)) {
        // It has not resumed or opened a window. Never launch without a guard.
        fprintf(stderr,"VQ3E_BOUNDED job_failed=%lu\n",GetLastError());
        TerminateProcess(process.hProcess,5);
        CloseHandle(process.hThread); CloseHandle(process.hProcess); CloseHandle(job); return 5;
    }
    ULONGLONG start = GetTickCount64();
    printf("VQ3E_BOUNDED pid=%lu deadline_ms=%lu owner=%lu\n",process.dwProcessId,timeout_ms,GetCurrentProcessId()); fflush(stdout);
    if (ResumeThread(process.hThread)==(DWORD)-1) {
        CloseHandle(process.hThread); CloseHandle(process.hProcess); CloseHandle(job); return 6;
    }
    CloseHandle(process.hThread);
    // Opt-in, one-shot focus request for an attended FG functional test. Never
    // keep stealing focus if the user switches away, and never touch other apps.
    const wchar_t *foreground = _wgetenv(L"VQ3E_BOUNDED_FOREGROUND");
    if (foreground && !wcscmp(foreground,L"1")) {
        ForegroundTarget target = { process.dwProcessId, NULL };
        while (GetTickCount64()-start < timeout_ms && !target.window &&
            WaitForSingleObject(process.hProcess,50)==WAIT_TIMEOUT) {
            EnumWindows(find_game_window,(LPARAM)&target);
        }
        if (target.window) {
            BOOL requested = SetForegroundWindow(target.window);
            printf("VQ3E_BOUNDED foreground_requested=%d foreground_confirmed=%d\n",
                requested,GetForegroundWindow()==target.window); fflush(stdout);
        }
    }
    ULONGLONG elapsed = GetTickCount64()-start;
    DWORD result = WaitForSingleObject(process.hProcess,elapsed < timeout_ms ? timeout_ms-(DWORD)elapsed : 0), code=0;
    if (result!=WAIT_OBJECT_0) {
        TerminateJobObject(job,124);
        WaitForSingleObject(process.hProcess,2000);
        code=124;
    } else if (!GetExitCodeProcess(process.hProcess,&code)) code=7;
    printf("VQ3E_BOUNDED exit=%lu elapsed_ms=%llu timed_out=%u\n",code,
        (unsigned long long)(GetTickCount64()-start),result!=WAIT_OBJECT_0); fflush(stdout);
    CloseHandle(process.hProcess); CloseHandle(job);
    return code;
}

#ifndef PT_BOUNDED_TEST
int wmain(int argc,wchar_t **argv)
{
    if (argc!=5) { fwprintf(stderr,L"Usage: pt-bounded-process exe directory seconds arguments\n"); return 2; }
    wchar_t *end=NULL;
    unsigned long seconds=wcstoul(argv[3],&end,10);
    if (*end || seconds<1 || seconds>120) return 2;
    /* Nsight may stop forwarding stdout when collection ends. Keep the
     * independent deadline/exit evidence in the capture's private directory. */
    const wchar_t *log=_wgetenv(L"VQ3E_BOUNDED_LOG");
    if (log && *log && !_wfreopen(log,L"wx",stdout)) return 2;
    return (int)bounded_process(argv[1],argv[2],argv[4],(DWORD)seconds*1000,0);
}
#endif
