#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <string>
namespace {
bool nested=false;
__declspec(noinline) void fault() {
    __asm {
        mov edx, 1
        mov eax, 011223344h
        mov ecx, 055667788h
        mov ebx, 013579bdfh
        mov esi, 02468ace0h
        mov edi, 010203040h
        mov dword ptr [edx], eax
    }
}
__declspec(noinline) void second_fault() { *reinterpret_cast<volatile int*>(3)=19; }
LONG CALLBACK nested_handler(EXCEPTION_POINTERS* e) {
    if(e->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION&&!nested) { nested=true;second_fault(); }
    return EXCEPTION_CONTINUE_SEARCH;
}
DWORD WINAPI worker(void*) { std::printf("worker_tid=%lu\n",GetCurrentThreadId());std::fflush(stdout);fault();return 0; }
DWORD WINAPI ordinary(void*) { return 0; }
void handled() {
    __try { fault(); } __except(EXCEPTION_EXECUTE_HANDLER) { std::puts("handled_av_reached"); }
}
void partial() {
    auto p=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!p) ExitProcess(81);DWORD old=0;VirtualProtect(p+4096,4096,PAGE_NOACCESS,&old);
    memset(p+4088,0xab,8);ULONG_PTR args[]={0,reinterpret_cast<ULONG_PTR>(p+4088)};
    __try { RaiseException(EXCEPTION_ACCESS_VIOLATION,0,2,args); }
    __except(EXCEPTION_EXECUTE_HANDLER) { std::puts("partial_handler_reached"); }
    VirtualFree(p,0,MEM_RELEASE);
}
}
int wmain(int argc,wchar_t** argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);std::setvbuf(stderr,nullptr,_IONBF,0);
    if(argc<3) return 80;
    std::printf("main_tid=%lu\n",GetCurrentThreadId());
    OutputDebugStringW(L"owned fixture debug string");
    for(int i=3;i<argc;++i) { int n=WideCharToMultiByte(CP_UTF8,0,argv[i],-1,nullptr,0,nullptr,nullptr);std::string s(n,'\0');WideCharToMultiByte(CP_UTF8,0,argv[i],-1,s.data(),n,nullptr,nullptr);std::printf("arg%d=%s\n",i-3,s.c_str()); }
    std::wstring mode=argv[1];
    if(mode==L"handled") handled();
    else if(mode==L"handled-then-av") {handled();fault();}
    else if(mode==L"av") fault();
    else if(mode==L"nested") {AddVectoredExceptionHandler(1,nested_handler);fault();}
    else if(mode==L"worker") {HANDLE t=CreateThread(nullptr,0,worker,nullptr,0,nullptr);WaitForSingleObject(t,INFINITE);CloseHandle(t);}
    else if(mode==L"breakpoint") DebugBreak();
    else if(mode==L"partial") partial();
    else if(mode==L"stdin") {char data[128]{};DWORD got=0;if(!ReadFile(GetStdHandle(STD_INPUT_HANDLE),data,sizeof(data)-1,&got,nullptr)) return 85;std::printf("fixture_stdin=%s\n",data);}
    else if(mode==L"module"&&argc>3) {HMODULE m=LoadLibraryW(argv[3]);if(!m) return 82;auto f=reinterpret_cast<int(*)()>(GetProcAddress(m,"fixture_value"));if(!f||f()!=73) return 83;FreeLibrary(m);}
    else if(mode==L"churn") {for(int i=0;i<20;++i) {HANDLE t=CreateThread(nullptr,0,ordinary,nullptr,0,nullptr);WaitForSingleObject(t,INFINITE);CloseHandle(t);}}
    std::fprintf(stderr,"fixture_stderr\n");
    Sleep(mode==L"resident"?1250:100);
    HANDLE f=CreateFileW(argv[2],GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,0,nullptr);DWORD got=0;
    if(f==INVALID_HANDLE_VALUE) return 84;WriteFile(f,"fixture_completed",17,&got,nullptr);FlushFileBuffers(f);CloseHandle(f);
    std::puts("fixture_completed");return 37;
}
