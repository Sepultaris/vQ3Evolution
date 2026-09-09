#define PT_BOUNDED_TEST
#include "../tools/pt-bounded-process.c"
#include <assert.h>
int wmain(int argc,wchar_t **argv)
{
    if (argc==2 && !wcscmp(argv[1],L"--sleep")) { Sleep(10000); return 0; }
    if (argc==2 && !wcscmp(argv[1],L"--exit")) return 23;
    if (argc==2 && !wcscmp(argv[1],L"--delay")) { Sleep(250); return 23; }
    if (argc==3 && !wcscmp(argv[1],L"--argument")) return wcscmp(argv[2],L"space \"quoted\" C:\\tail\\") ? 99 : 23;
    wchar_t exe[32768],directory[32768];
    assert(GetModuleFileNameW(NULL,exe,32768));
    assert(GetCurrentDirectoryW(32768,directory));
    ULONGLONG start=GetTickCount64();
    assert(bounded_process(exe,directory,L"--sleep",150,CREATE_NO_WINDOW)==124);
    assert(GetTickCount64()-start<2500);
    assert(bounded_process(exe,directory,L"--exit",2000,CREATE_NO_WINDOW)==23);
    assert(bounded_process(exe,directory,L"--exit",90000,CREATE_NO_WINDOW)==23);
    assert(bounded_process(exe,directory,L"--sleep",120001,CREATE_NO_WINDOW)==2);
    assert(bounded_process(L"C:\\not-a-real-vq3e-test.exe",directory,L"",100,CREATE_NO_WINDOW)==4);
    puts("PASS: owned job timeout, normal exit, hard limit and failed launch; no game/GPU used");
    return 0;
}
