/* Windows-only diagnostic launcher. Debugs ONLY its newly created child;
 * never attaches to an existing game. No dump/upload or registry settings.
 * Build: gcc tools/pt-crash-capture.c -o <build>/pt-crash-capture.exe
 * Args: exe working-directory symbol-directory timeout-seconds game-command-line [--visible]
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static BOOL (WINAPI *symInit)(HANDLE, PCSTR, BOOL);
static BOOL (WINAPI *symCleanup)(HANDLE);
static DWORD (WINAPI *symOptions)(DWORD);
static DWORD64 (WINAPI *symLoad)(HANDLE,HANDLE,PCSTR,PCSTR,DWORD64,DWORD,PMODLOAD_DATA,DWORD);
static BOOL (WINAPI *symUnload)(HANDLE,DWORD64);
static BOOL (WINAPI *symAddress)(HANDLE,DWORD64,PDWORD64,PSYMBOL_INFO);
static BOOL (WINAPI *symModule)(HANDLE,DWORD64,PIMAGEHLP_MODULE64);
static PVOID (WINAPI *symFunction)(HANDLE,DWORD64);
static DWORD64 (WINAPI *symBase)(HANDLE,DWORD64);
static BOOL (WINAPI *stackWalk)(DWORD,HANDLE,HANDLE,LPSTACKFRAME64,PVOID,
    PREAD_PROCESS_MEMORY_ROUTINE64,PFUNCTION_TABLE_ACCESS_ROUTINE64,
    PGET_MODULE_BASE_ROUTINE64,PTRANSLATE_ADDRESS_ROUTINE64);
typedef struct { DWORD64 base; DWORD size; char path[4096]; } Module;
static Module modules[1024], unloaded[128];
static unsigned moduleCount;
static unsigned unloadedCount;
static int symbolsLoaded;
static ULONGLONG captureDeadline;

typedef struct {
    HANDLE process, cancel;
    ULONGLONG deadline;
    volatile LONG fired;
} Watchdog;

/* Stack walking/symbol loading can block. Never let diagnostic work hold the
 * game open past the authorized deadline. Only this launcher's child is owned. */
static DWORD WINAPI watchdog_main(LPVOID data)
{
    Watchdog *watchdog = data;
    ULONGLONG now = GetTickCount64();
    DWORD remaining = now < watchdog->deadline ? (DWORD)(watchdog->deadline-now) : 0;
    if (WaitForSingleObject(watchdog->cancel,remaining) == WAIT_TIMEOUT) {
        InterlockedExchange(&watchdog->fired,1);
        TerminateProcess(watchdog->process,124);
    }
    return 0;
}

static void remember_module(HANDLE process,HANDLE file,DWORD64 base,const char *fallback)
{
    if (moduleCount>=1024) return;
    modules[moduleCount].base=base;
    modules[moduleCount].path[0]=0;
    modules[moduleCount].size=0;
    IMAGE_DOS_HEADER dos={0}; IMAGE_NT_HEADERS64 nt={0}; SIZE_T read=0;
    if (ReadProcessMemory(process,(LPCVOID)base,&dos,sizeof(dos),&read) &&
        dos.e_magic==IMAGE_DOS_SIGNATURE &&
        ReadProcessMemory(process,(LPCVOID)(base+dos.e_lfanew),&nt,sizeof(nt),&read) &&
        nt.Signature==IMAGE_NT_SIGNATURE) modules[moduleCount].size=nt.OptionalHeader.SizeOfImage;
    DWORD length=file ? GetFinalPathNameByHandleA(file,modules[moduleCount].path,4096,FILE_NAME_NORMALIZED):0;
    if ((!length || length>=4096) && fallback)
        snprintf(modules[moduleCount].path,4096,"%s",fallback);
    if (!strncmp(modules[moduleCount].path,"\\\\?\\",4))
        memmove(modules[moduleCount].path,modules[moduleCount].path+4,
            strlen(modules[moduleCount].path+4)+1);
    ++moduleCount;
    symbolsLoaded=0;
}

static void print_address(HANDLE process, DWORD64 address)
{
    union { SYMBOL_INFO info; char bytes[sizeof(SYMBOL_INFO)+1024]; } storage = {0};
    SYMBOL_INFO *symbol = &storage.info;
    IMAGEHLP_MODULE64 module = {0};
    DWORD64 offset = 0;
    symbol->SizeOfStruct = sizeof(*symbol); symbol->MaxNameLen = 1023;
    module.SizeOfStruct = sizeof(module);
    printf("  0x%016llx", (unsigned long long)address);
    if (symModule(process,address,&module))
        printf(" %s+0x%llx",module.ModuleName,(unsigned long long)(address-module.BaseOfImage));
    else for (unsigned i=unloadedCount;i>0;--i) {
        const Module *old=&unloaded[i-1];
        if (address>=old->base && address-old->base<old->size) {
            printf(" [UNLOADED %s+0x%llx]",old->path,(unsigned long long)(address-old->base));
            break;
        }
    }
    if (symAddress(process,address,&offset,symbol))
        printf(" %s+0x%llx",symbol->Name,(unsigned long long)offset);
    puts("");
}

static void capture_stack(HANDLE process, DWORD threadId, const EXCEPTION_DEBUG_INFO *exception)
{
    if (GetTickCount64() >= captureDeadline) return;
    HANDLE thread = OpenThread(THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,threadId);
    CONTEXT context = {0};
    STACKFRAME64 frame = {0};
    printf("%s exception=0x%08lx firstChance=%lu thread=%lu parameters=",
        exception->ExceptionRecord.ExceptionCode ? "PT_CRASH":"PT_THREAD",
        exception->ExceptionRecord.ExceptionCode,exception->dwFirstChance,threadId);
    for (DWORD i=0;i<exception->ExceptionRecord.NumberParameters;++i)
        printf("0x%llx ",(unsigned long long)exception->ExceptionRecord.ExceptionInformation[i]);
    puts("");
    // Avoid symbol work during ordinary startup/restarts: it perturbs timing
    // and can add seconds for driver modules. Load current modules only now.
    fflush(stdout);
    if (!symbolsLoaded) {
        for (unsigned i=0;i<moduleCount;++i) {
            if (GetTickCount64() >= captureDeadline) break;
            DWORD size=modules[i].size;
            printf("PT_MODULE 0x%llx size=0x%lx %s\n",(unsigned long long)modules[i].base,size,modules[i].path);
            if (modules[i].path[0]) symLoad(process,NULL,modules[i].path,NULL,modules[i].base,size,NULL,0);
        }
        symbolsLoaded=1;
    }
    print_address(process,(DWORD64)exception->ExceptionRecord.ExceptionAddress);
    context.ContextFlags = CONTEXT_FULL;
    if (thread && GetThreadContext(thread,&context)) {
        frame.AddrPC.Offset=context.Rip; frame.AddrStack.Offset=context.Rsp; frame.AddrFrame.Offset=context.Rbp;
        frame.AddrPC.Mode=frame.AddrStack.Mode=frame.AddrFrame.Mode=AddrModeFlat;
        for (int i=0;i<48;++i) {
            if (GetTickCount64() >= captureDeadline) break;
            if (!stackWalk(IMAGE_FILE_MACHINE_AMD64,process,thread,&frame,&context,NULL,symFunction,symBase,NULL) || !frame.AddrPC.Offset) break;
            print_address(process,frame.AddrPC.Offset);
        }
    }
    if (thread) CloseHandle(thread);
    fflush(stdout);
}

static void capture_hang(HANDLE process,DWORD processId)
{
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    THREADENTRY32 entry={0}; entry.dwSize=sizeof(entry);
    if (snapshot==INVALID_HANDLE_VALUE) return;
    if (Thread32First(snapshot,&entry)) do {
        if (GetTickCount64() >= captureDeadline) break;
        if (entry.th32OwnerProcessID!=processId) continue;
        HANDLE thread=OpenThread(THREAD_SUSPEND_RESUME,FALSE,entry.th32ThreadID);
        if (thread && SuspendThread(thread)!=(DWORD)-1) {
            EXCEPTION_DEBUG_INFO info={0};
            puts("PT_HANG thread snapshot");
            capture_stack(process,entry.th32ThreadID,&info);
            ResumeThread(thread);
        }
        if (thread) CloseHandle(thread);
    } while(Thread32Next(snapshot,&entry));
    CloseHandle(snapshot);
}

int main(int argc,char **argv)
{
    if (argc!=6 && (argc!=7 || strcmp(argv[6],"--visible"))) {
        fputs("Expected exe, cwd, symbols, timeout, one quoted command-line argument, and optional --visible\n",stderr);
        return 2;
    }
    char *timeoutEnd;
    unsigned long seconds=strtoul(argv[4],&timeoutEnd,10);
    if (!argv[4][0] || *timeoutEnd || seconds<1 || seconds>300) {
        fputs("Timeout must be an integer from 1 to 300 seconds\n",stderr);
        return 2;
    }
    HMODULE dbg = LoadLibraryExA("C:\\Program Files (x86)\\Windows Kits\\10\\Debuggers\\x64\\dbghelp.dll",
        NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dbg) { fprintf(stderr,"Cannot load SDK dbghelp: %lu\n",GetLastError()); return 2; }
#define LOAD(variable,name) do { FARPROC address=GetProcAddress(dbg,name); memcpy(&variable,&address,sizeof(variable)); if (!variable) return 2; } while(0)
    LOAD(symInit,"SymInitialize"); LOAD(symCleanup,"SymCleanup"); LOAD(symOptions,"SymSetOptions");
    LOAD(symLoad,"SymLoadModuleEx"); LOAD(symUnload,"SymUnloadModule64"); LOAD(symAddress,"SymFromAddr");
    LOAD(symModule,"SymGetModuleInfo64"); LOAD(symFunction,"SymFunctionTableAccess64");
    LOAD(symBase,"SymGetModuleBase64"); LOAD(stackWalk,"StackWalk64");
    STARTUPINFOA startup={0}; PROCESS_INFORMATION process={0};
    startup.cb=sizeof(startup); startup.dwFlags=STARTF_USESHOWWINDOW;
    startup.wShowWindow=argc==7 ? SW_SHOWNORMAL:SW_HIDE;
    size_t size=strlen(argv[1])+strlen(argv[5])+4;
    char *command=malloc(size);
    if (!command) return 2;
    snprintf(command,size,"\"%s\" %s",argv[1],argv[5]);
    // Match a normal launch's heap behavior. Windows' debugger-only heap
    // checks make the graphics driver's restart allocations extremely slow.
    // This affects only this helper and its child, never system settings.
    SetEnvironmentVariableA("_NO_DEBUG_HEAP","1");
    ULONGLONG start=GetTickCount64(),limit=(ULONGLONG)seconds*1000;
    if (!CreateProcessA(argv[1],command,NULL,NULL,FALSE,DEBUG_ONLY_THIS_PROCESS|CREATE_NO_WINDOW,
        NULL,argv[2],&startup,&process)) { fprintf(stderr,"CreateProcess failed: %lu\n",GetLastError()); free(command); return 2; }
    free(command);
    Watchdog watchdog={process.hProcess,CreateEventA(NULL,TRUE,FALSE,NULL),start+limit,0};
    captureDeadline=watchdog.deadline;
    HANDLE watchdogThread=watchdog.cancel ? CreateThread(NULL,0,watchdog_main,&watchdog,0,NULL):NULL;
    if (!watchdogThread) {
        TerminateProcess(process.hProcess,2);
        if (watchdog.cancel) CloseHandle(watchdog.cancel);
        CloseHandle(process.hThread); CloseHandle(process.hProcess); FreeLibrary(dbg);
        return 2;
    }
    symOptions(SYMOPT_LOAD_LINES|SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS|SYMOPT_FAIL_CRITICAL_ERRORS|SYMOPT_NO_PROMPTS);
    if (!symInit(process.hProcess,argv[3],FALSE)) {
        TerminateProcess(process.hProcess,2);
        SetEvent(watchdog.cancel); WaitForSingleObject(watchdogThread,INFINITE);
        CloseHandle(watchdogThread); CloseHandle(watchdog.cancel);
        CloseHandle(process.hThread); CloseHandle(process.hProcess); FreeLibrary(dbg);
        return 2;
    }
    printf("PT_DEBUG child=%lu\n",process.dwProcessId); fflush(stdout);
    int initialBreakpoint=1,done=0,exitCode=1,capturedHang=0;
    while (!done) {
        DEBUG_EVENT event;
        // Reserve five seconds for a bounded hang snapshot. The independent
        // watchdog still kills the child even if DbgHelp never returns.
        if (!capturedHang && GetTickCount64()-start >= (limit>5000 ? limit-5000:limit/2)) {
            capturedHang=1;
            capture_hang(process.hProcess,process.dwProcessId);
        }
        if (!WaitForDebugEvent(&event,1000)) {
            if (GetLastError()==ERROR_SEM_TIMEOUT) continue;
            fprintf(stderr,"WaitForDebugEvent failed: %lu\n",GetLastError());
            TerminateProcess(process.hProcess,2); break;
        }
        DWORD disposition=DBG_CONTINUE;
        switch(event.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
            remember_module(process.hProcess,event.u.CreateProcessInfo.hFile,(DWORD64)event.u.CreateProcessInfo.lpBaseOfImage,argv[1]);
            if (event.u.CreateProcessInfo.hFile) CloseHandle(event.u.CreateProcessInfo.hFile);
            break;
        case LOAD_DLL_DEBUG_EVENT:
            remember_module(process.hProcess,event.u.LoadDll.hFile,(DWORD64)event.u.LoadDll.lpBaseOfDll,NULL);
            if (event.u.LoadDll.hFile) CloseHandle(event.u.LoadDll.hFile);
            break;
        case UNLOAD_DLL_DEBUG_EVENT:
            for (unsigned i=0;i<moduleCount;++i) if (modules[i].base==(DWORD64)event.u.UnloadDll.lpBaseOfDll) {
                if (unloadedCount==128) {
                    memmove(unloaded,unloaded+1,127*sizeof(*unloaded)); --unloadedCount;
                }
                unloaded[unloadedCount++]=modules[i];
                symUnload(process.hProcess,modules[i].base);
                modules[i]=modules[--moduleCount]; break;
            }
            break;
        case CREATE_THREAD_DEBUG_EVENT: CloseHandle(event.u.CreateThread.hThread); break;
        case EXCEPTION_DEBUG_EVENT:
            if (initialBreakpoint && event.u.Exception.ExceptionRecord.ExceptionCode==EXCEPTION_BREAKPOINT) initialBreakpoint=0;
            else {
                // Capture the original AV before the game's signal handler
                // attempts teardown of a partially initialized renderer.
                if (!event.u.Exception.dwFirstChance || event.u.Exception.ExceptionRecord.ExceptionCode==0xc0000409 ||
                    event.u.Exception.ExceptionRecord.ExceptionCode==EXCEPTION_ACCESS_VIOLATION)
                    capture_stack(process.hProcess,event.dwThreadId,&event.u.Exception);
                disposition=DBG_EXCEPTION_NOT_HANDLED;
            }
            break;
        case EXIT_PROCESS_DEBUG_EVENT:
            printf("PT_DEBUG exit=0x%08lx\n",event.u.ExitProcess.dwExitCode);
            exitCode=event.u.ExitProcess.dwExitCode ? 1:0; done=1; break;
        }
        ContinueDebugEvent(event.dwProcessId,event.dwThreadId,disposition);
    }
    SetEvent(watchdog.cancel); WaitForSingleObject(watchdogThread,INFINITE);
    if (watchdog.fired) {
        puts("PT_DEBUG watchdog: terminated owned child at deadline");
        exitCode=124;
    }
    CloseHandle(watchdogThread); CloseHandle(watchdog.cancel);
    symCleanup(process.hProcess); CloseHandle(process.hThread); CloseHandle(process.hProcess); FreeLibrary(dbg);
    return exitCode;
}
