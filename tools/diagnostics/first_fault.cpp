// External x86 observer. It never links or calls the port or its guest image.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <new>
#include <sstream>
#include <string>
#include <stdexcept>
#include <vector>
static_assert(sizeof(void*) == 4, "The observer must be native i386");
namespace {
std::atomic<bool> cancelled{};
BOOL WINAPI cancel(DWORD) { cancelled=true; return TRUE; }
std::string utf8(const std::wstring& s) {
    if(s.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
    std::string r(n,'\0'); WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),r.data(),n,nullptr,nullptr); return r;
}
std::string j(const std::string& s) {
    std::string r="\""; char b[8];
    for(unsigned char c:s) { if(c=='"'||c=='\\') { r+='\\';r+=char(c); }
        else if(c<32) { std::snprintf(b,sizeof b,"\\u%04x",c);r+=b; } else r+=char(c); }
    return r+'"';
}
std::string jw(const std::wstring& s) { return j(utf8(s)); }
std::string hx(uint32_t v) { char b[16];std::snprintf(b,sizeof b,"0x%08x",v);return j(b); }
std::string bytes(const void* p,size_t n) {
    auto d=static_cast<const unsigned char*>(p);std::string s; s.reserve(n*2);
    for(size_t i=0;i<n;++i) { s+="0123456789abcdef"[d[i]>>4];s+="0123456789abcdef"[d[i]&15]; } return j(s);
}
std::wstring quote(const std::wstring& s) {
    std::wstring r=L"\""; size_t slash=0;
    for(wchar_t c:s) { if(c==L'\\') { ++slash;continue; }
        r.append(c==L'"'?slash*2+1:slash,L'\\');slash=0;r+=c; }
    r.append(slash*2,L'\\');return r+L'"';
}
std::wstring full(const std::wstring& s) {
    std::vector<wchar_t> b(32768);DWORD n=GetFullPathNameW(s.c_str(),DWORD(b.size()),b.data(),nullptr);
    if(!n||n>=b.size()) throw std::runtime_error("Cannot resolve path");return {b.data(),n};
}
bool valid(HANDLE h) { return h&&h!=INVALID_HANDLE_VALUE; }
void close(HANDLE& h) noexcept { if(valid(h)) CloseHandle(h);h=nullptr; }
HANDLE dup(HANDLE h) { HANDLE out=nullptr;DuplicateHandle(GetCurrentProcess(),h,GetCurrentProcess(),&out,0,FALSE,DUPLICATE_SAME_ACCESS);return out; }
std::string sha(const std::wstring& path) {
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) throw std::runtime_error("SHA provider failed");
    DWORD size=0,got=0;BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),4,&got,0);
    std::vector<unsigned char> object(size);unsigned char digest[32]{};
    if(BCryptCreateHash(alg,&hash,object.data(),size,nullptr,0,0)<0) { BCryptCloseAlgorithmProvider(alg,0);throw std::runtime_error("SHA creation failed"); }
    std::ifstream f(std::filesystem::path(path),std::ios::binary);char block[65536];
    if(!f) { BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);throw std::runtime_error("Cannot hash file"); }
    while(f) { f.read(block,sizeof block);auto n=f.gcount();if(n&&BCryptHashData(hash,reinterpret_cast<PUCHAR>(block),ULONG(n),0)<0) throw std::runtime_error("SHA update failed"); }
    BCryptFinishHash(hash,digest,32,0);BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);
    auto text=bytes(digest,32);return text.substr(1,text.size()-2);
}
uint32_t u32(const unsigned char* p) { uint32_t v;memcpy(&v,p,4);return v; }
uint16_t u16(const unsigned char* p) { uint16_t v;memcpy(&v,p,2);return v; }
uint32_t pe(const unsigned char* p,size_t n) {
    if(n<84||u16(p)!=0x5a4d) return 0;uint32_t at=u32(p+60);
    if(at>n-84||u32(p+at)!=0x4550||u16(p+at+4)!=0x14c||u16(p+at+24)!=0x10b) return 0;
    return u32(p+at+24+56);
}
void verify_file(const std::wstring& p,uint64_t size,const std::string& hash) {
    std::ifstream f(std::filesystem::path(p),std::ios::binary);unsigned char header[4096]{};f.read(reinterpret_cast<char*>(header),sizeof header);
    if(std::filesystem::file_size(p)!=size||sha(p)!=hash||!pe(header,size_t(f.gcount()))) throw std::runtime_error("File size/hash/PE32 gate rejected");
}
struct Config { std::wstring root,exe,prefix;std::vector<std::wstring> args;std::string failure;bool fixture=false; };
struct Module { uint32_t base=0,size=0,timestamp=0,path_error=0;std::wstring path;HANDLE retry=nullptr;bool system=false,path_truncated=false;BY_HANDLE_FILE_INFORMATION file{};bool file_id=false; };
struct Observer {
    Config c;HANDLE log=nullptr,process=nullptr;PROCESS_INFORMATION pi{};
    std::map<DWORD,HANDLE> threads;std::map<uint32_t,Module> modules;
    BY_HANDLE_FILE_INFORMATION system_file{};
    bool active=false,pending=false,setup=false,incomplete=false,detached=false,exit_seen=false;
    bool startup=false,metadata_bad=false,first_bp=false,meta_bp_retry=false,output_ok=true;
    bool first_exception=true,failed_context=false,failed_output=false,failed_continue=false,failed_throw=false,failed_flush=false,failed_ntdll_throw=false;
    DWORD pid=0,primary=0,exit_event=0,exit_code=0,detach_attempts=0,detach_pending=0;
    DWORD last_error=0;uint64_t sequence=0,written=0;unsigned exceptions=0;
    DWORD saved_disposition=0;
    DEBUG_EVENT event{};const char* reason="";std::string verified="[]";
    // Reserve 64 KiB of the global 16 MiB bound for summary/attention channels.
    static constexpr uint64_t budget=16*1024*1024-64*1024;
    uint64_t current_budget=budget;bool budget_injected=false;
    ULONGLONG last_attention=0;DWORD attention_count=0;
    void attention(const char* phase,DWORD error,DWORD status) noexcept {
        ULONGLONG now=GetTickCount64();if(attention_count>=64||(attention_count&&now-last_attention<1000)) return;
        last_attention=now;++attention_count;
        std::fprintf(stderr,"observer resident attention: phase=%s error=%lu pid=%lu tid=%lu event=%lu pending=%u disposition=0x%08lx disposition_known=%u kill_disabled=%u\n",phase,error,pid,event.dwThreadId,event.dwDebugEventCode,pending?1u:0u,status,status?1u:0u,setup?1u:0u);std::fflush(stderr);
    }
    // Every reason is a static literal; the active-debugger failure path allocates nothing.
    void fail(const char* why) noexcept { incomplete=true;if(!reason[0]) reason=why; }
    bool emit(const std::string& text,bool durable=false) {
        if(!output_ok) return false;
        SYSTEMTIME utc{};GetSystemTime(&utc);char stamp[40];std::snprintf(stamp,sizeof stamp,"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",utc.wYear,utc.wMonth,utc.wDay,utc.wHour,utc.wMinute,utc.wSecond,utc.wMilliseconds);
        auto s="{\"seq\":"+std::to_string(++sequence)+",\"utc\":"+j(stamp)+",\"ticks\":"+std::to_string(GetTickCount64())+","+text+"}\n";
        if(c.failure=="budget"&&startup&&!budget_injected) {current_budget=written+64;budget_injected=true;}
        if(s.size()>32768||written+s.size()>current_budget) { fail("global_event_budget");output_ok=false;return false; }
        if(c.failure=="output"&&startup&&!failed_output) { failed_output=true;fail("injected_output_failure");output_ok=false;return false; }
        DWORD at=0;
        while(at<s.size()) { DWORD n=0;if(!WriteFile(log,s.data()+at,DWORD(s.size()-at),&n,nullptr)||!n) { fail("event_write_failure");output_ok=false;return false; }at+=n; }
        written+=s.size();
        if(durable&&startup&&c.failure=="flush"&&!failed_flush) {failed_flush=true;fail("injected_flush_failure");output_ok=false;return false;}
        if(durable&&!FlushFileBuffers(log)) { fail("event_flush_failure");output_ok=false;return false; }return true;
    }
    bool same_file(HANDLE file) {
        BY_HANDLE_FILE_INFORMATION i{};return GetFileInformationByHandle(file,&i)&&i.dwVolumeSerialNumber==system_file.dwVolumeSerialNumber&&i.nFileIndexHigh==system_file.nFileIndexHigh&&i.nFileIndexLow==system_file.nFileIndexLow;
    }
    bool metadata(Module& m) {
        if(!valid(m.retry)) return false;
        bool sys=same_file(m.retry);
        if(c.failure=="metadata"&&sys&&(!first_bp||!meta_bp_retry)) { if(first_bp) meta_bp_retry=true;metadata_bad=true;return false; }
        wchar_t p[1100]{};DWORD n=GetFinalPathNameByHandleW(m.retry,p,1100,FILE_NAME_NORMALIZED);
        if(n&&n<1100) {m.path.assign(p,std::min<DWORD>(n,1024));m.path_truncated=n>1024;}
        else {m.path_error=n?ERROR_INSUFFICIENT_BUFFER:GetLastError();m.path_truncated=n>=1100;}
        m.file_id=GetFileInformationByHandle(m.retry,&m.file)!=FALSE;
        unsigned char header[4096]{};SIZE_T got=0;
        if(!ReadProcessMemory(process,reinterpret_cast<void*>(m.base),header,sizeof header,&got)) { last_error=GetLastError();return false; }
        m.size=pe(header,got);if(m.size) m.timestamp=u32(header+u32(header+60)+8);m.system=sys&&m.size!=0;close(m.retry);return m.size!=0;
    }
    bool retry_metadata() {
        bool okay=true;for(auto& [base,m]:modules) {
            if(valid(m.retry)) {
                if(!metadata(m)) okay=false;
                else emit("\"kind\":\"module_metadata\",\"base\":"+hx(base)+",\"size\":"+std::to_string(m.size)+",\"timestamp\":"+std::to_string(m.timestamp)+",\"path\":"+(m.path.empty()?"null":jw(m.path))+",\"path_error\":"+std::to_string(m.path_error)+",\"path_truncated\":"+(m.path_truncated?"true":"false")+",\"system_ntdll\":"+(m.system?"true":"false"));
            }
        }return okay;
    }
    void add_thread(DWORD id,HANDLE source) {
        if(threads.size()>=4096) {fail("thread_count_limit");return;}
        HANDLE owned=dup(source);if(!owned) {fail("thread_duplicate_failed");return;}
        try {auto inserted=threads.emplace(id,owned).second;if(!inserted) {close(owned);fail("duplicate_live_tid");}}
        catch(...) {close(owned);throw;}
    }
    bool initial() noexcept {
        auto& e=event.u.Exception;
        if(startup||!first_exception||event.dwProcessId!=pid||event.dwThreadId!=primary||!e.dwFirstChance||e.ExceptionRecord.ExceptionCode!=EXCEPTION_BREAKPOINT) return false;
        uint32_t ip=reinterpret_cast<uint32_t>(e.ExceptionRecord.ExceptionAddress);
        for(auto& [base,m]:modules) if(m.system&&ip>=base&&uint64_t(ip)<uint64_t(base)+m.size) return true;
        return false;
    }
    std::string memory(uint32_t address,size_t count) {
        MEMORY_BASIC_INFORMATION q{};SIZE_T n=VirtualQueryEx(process,reinterpret_cast<void*>(address),&q,sizeof q);
        if(!n) return "{\"address\":"+hx(address)+",\"query_error\":"+std::to_string(GetLastError())+"}";
        uint64_t end=uint64_t(reinterpret_cast<uint32_t>(q.BaseAddress))+q.RegionSize;
        size_t ask=size_t(std::min<uint64_t>(count,std::min<uint64_t>(UINT32_MAX-uint64_t(address)+1,end>address?end-address:0)));
        unsigned char data[256]{};SIZE_T got=0;SetLastError(0);
        BOOL ok=ask&&ReadProcessMemory(process,reinterpret_cast<void*>(address),data,ask,&got);
        DWORD error=ok?0:GetLastError();
        return "{\"address\":"+hx(address)+",\"base\":"+hx(reinterpret_cast<uint32_t>(q.BaseAddress))+",\"allocation_base\":"+hx(reinterpret_cast<uint32_t>(q.AllocationBase))+",\"region_size\":"+std::to_string(q.RegionSize)+",\"state\":"+hx(q.State)+",\"protect\":"+hx(q.Protect)+",\"type\":"+hx(q.Type)+",\"requested\":"+std::to_string(count)+",\"bounded\":"+std::to_string(ask)+",\"read\":"+std::to_string(got)+",\"error\":"+std::to_string(error)+",\"bytes\":"+bytes(data,got)+"}";
    }
    void exception(bool os_startup) {
        if(!os_startup) {if(exceptions>=512) {fail("exception_count_limit");return;}++exceptions;}
        auto& info=event.u.Exception;auto& e=info.ExceptionRecord;
        CONTEXT x{};x.ContextFlags=CONTEXT_CONTROL|CONTEXT_INTEGER|CONTEXT_SEGMENTS|CONTEXT_FLOATING_POINT|CONTEXT_EXTENDED_REGISTERS;
        auto it=threads.find(event.dwThreadId);BOOL okay=FALSE;DWORD error=0;
        if(os_startup&&c.failure=="context"&&!failed_context) { failed_context=true;error=ERROR_ACCESS_DENIED; }
        else if(it!=threads.end()) { okay=GetThreadContext(it->second,&x);if(!okay) error=GetLastError(); }
        else error=ERROR_INVALID_HANDLE;
        if(!okay) fail("context_unavailable");
        std::string ctx="null";
        if(okay) {
            ctx="{\"flags\":"+hx(x.ContextFlags)+",\"eip\":"+hx(x.Eip)+",\"esp\":"+hx(x.Esp)+",\"ebp\":"+hx(x.Ebp)+",\"eflags\":"+hx(x.EFlags);
            ctx+=",\"eax\":"+hx(x.Eax)+",\"ebx\":"+hx(x.Ebx)+",\"ecx\":"+hx(x.Ecx)+",\"edx\":"+hx(x.Edx)+",\"esi\":"+hx(x.Esi)+",\"edi\":"+hx(x.Edi);
            ctx+=",\"cs\":"+hx(x.SegCs)+",\"ss\":"+hx(x.SegSs)+",\"ds\":"+hx(x.SegDs)+",\"es\":"+hx(x.SegEs)+",\"fs\":"+hx(x.SegFs)+",\"gs\":"+hx(x.SegGs)+",\"x87\":"+bytes(&x.FloatSave,sizeof x.FloatSave)+",\"extended\":"+bytes(x.ExtendedRegisters,sizeof x.ExtendedRegisters)+"}";
        }
        std::string params="[";for(DWORD i=0;i<std::min<DWORD>(e.NumberParameters,EXCEPTION_MAXIMUM_PARAMETERS);++i) { if(i) params+=',';params+=hx(e.ExceptionInformation[i]); }params+=']';
        std::string s="\"kind\":\"exception\",\"pid\":"+std::to_string(event.dwProcessId)+",\"tid\":"+std::to_string(event.dwThreadId)+",\"code\":"+hx(e.ExceptionCode)+",\"flags\":"+hx(e.ExceptionFlags)+",\"linked_record_address\":"+(e.ExceptionRecord?hx(reinterpret_cast<uint32_t>(e.ExceptionRecord)):"null")+",\"address\":"+hx(reinterpret_cast<uint32_t>(e.ExceptionAddress))+",\"first_chance\":"+std::to_string(info.dwFirstChance)+",\"os_startup\":"+(os_startup?"true":"false")+",\"parameter_count\":"+std::to_string(e.NumberParameters)+",\"parameters_truncated\":"+(e.NumberParameters>EXCEPTION_MAXIMUM_PARAMETERS?"true":"false")+",\"parameters\":"+params+",\"context\":"+ctx+",\"context_error\":"+std::to_string(error);
        if(!os_startup) {
            s+=",\"ip_memory\":"+memory(okay?x.Eip:reinterpret_cast<uint32_t>(e.ExceptionAddress),32);
            s+=",\"stack_memory\":"+(okay?memory(x.Esp,256):"null");
            if(e.ExceptionCode==EXCEPTION_ACCESS_VIOLATION&&e.NumberParameters>=2) s+=",\"target_memory\":"+memory(e.ExceptionInformation[1],32);
        }
        if(os_startup) startup=true;emit(s,true);
        if(os_startup&&c.failure=="throw"&&!failed_throw) {failed_throw=true;throw std::bad_alloc();}
    }
    bool continuation(DWORD status) noexcept {
        saved_disposition=status;
        if((c.failure=="continue"||((c.failure=="throw"||c.failure=="throw-create")&&failed_throw))&&!failed_continue) { failed_continue=true;fail("injected_continue_failure");last_error=ERROR_ACCESS_DENIED;attention("continue",last_error,status);return false; }
        if(!ContinueDebugEvent(event.dwProcessId,event.dwThreadId,status)) { fail("continue_failure");last_error=GetLastError();attention("continue",last_error,status);return false; }
        pending=false;return true;
    }
    bool detach() noexcept {
        ++detach_attempts;if(pending) ++detach_pending;
        unsigned failures=(c.failure=="safety"||c.failure=="continue"||c.failure=="throw"||c.failure=="throw-create")?3u:((c.failure=="metadata"||c.failure=="ntdll-throw")?100u:0u);
        if(c.failure=="resident"||detach_attempts<=failures) { last_error=ERROR_ACCESS_DENIED;attention("detach",last_error,pending?saved_disposition:0);return false; }
        if(!DebugActiveProcessStop(pid)) { last_error=GetLastError();attention("detach",last_error,pending?saved_disposition:0);return false; }
        active=false;pending=false;detached=true;return true;
    }
    void add_module(uint32_t base,HANDLE file) {
        if(modules.size()>=512) { fail("module_count_limit");return; }
        Module m;m.base=base;if(valid(file)) m.retry=dup(file);
        try {
            if(!metadata(m)) { metadata_bad=true;fail("module_metadata_unavailable"); }
            if(c.failure=="ntdll-throw"&&m.system&&!failed_ntdll_throw) {failed_ntdll_throw=true;throw std::bad_alloc();}
            emit("\"kind\":\"module_load\",\"base\":"+hx(base)+",\"size\":"+(m.size?std::to_string(m.size):"null")+",\"timestamp\":"+(m.size?std::to_string(m.timestamp):"null")+",\"file_id_available\":"+(m.file_id?"true":"false")+",\"volume\":"+(m.file_id?std::to_string(m.file.dwVolumeSerialNumber):"null")+",\"file_index_high\":"+(m.file_id?std::to_string(m.file.nFileIndexHigh):"null")+",\"file_index_low\":"+(m.file_id?std::to_string(m.file.nFileIndexLow):"null")+",\"path\":"+(m.path.empty()?"null":jw(m.path))+",\"path_error\":"+std::to_string(m.path_error)+",\"path_truncated\":"+(m.path_truncated?"true":"false")+",\"system_ntdll\":"+(m.system?"true":"false"));
            modules.emplace(base,std::move(m));m.retry=nullptr;
        } catch(...) {metadata_bad=true;close(m.retry);throw;}
    }
    void run() {
        wchar_t directory[MAX_PATH]{};UINT n=GetSystemWow64DirectoryW(directory,MAX_PATH);
        if(!n) n=GetSystemDirectoryW(directory,MAX_PATH);if(!n||n>=MAX_PATH) throw std::runtime_error("Cannot locate system ntdll");
        std::wstring system(directory,n);system+=L"\\ntdll.dll";
        HANDLE sf=CreateFileW(system.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
        if(!valid(sf)||!GetFileInformationByHandle(sf,&system_file)) {DWORD error=GetLastError();close(sf);throw std::runtime_error("Cannot attest system ntdll file identity: "+utf8(system)+" error="+std::to_string(error));}close(sf);
        log=CreateFileW((c.prefix+L".events.jsonl").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,0,nullptr);
        if(!valid(log)) throw std::runtime_error("Cannot create observer event file");
        SECURITY_ATTRIBUTES sa{sizeof sa,nullptr,TRUE};
        HANDLE out=CreateFileW((c.prefix+L".stdout.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_NEW,0,nullptr);
        HANDLE err=CreateFileW((c.prefix+L".stderr.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_NEW,0,nullptr);
        HANDLE in=nullptr;HANDLE original_in=GetStdHandle(STD_INPUT_HANDLE);
        if(valid(original_in)) DuplicateHandle(GetCurrentProcess(),original_in,GetCurrentProcess(),&in,0,TRUE,DUPLICATE_SAME_ACCESS);
        else in=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);
        if(!valid(out)||!valid(err)||!valid(in)) { close(out);close(err);close(in);throw std::runtime_error("Cannot create redirected handles"); }
        STARTUPINFOEXW si{};si.StartupInfo.cb=sizeof si;si.StartupInfo.dwFlags=STARTF_USESTDHANDLES;si.StartupInfo.hStdOutput=out;si.StartupInfo.hStdError=err;si.StartupInfo.hStdInput=in;
        SIZE_T attr_size=0;InitializeProcThreadAttributeList(nullptr,1,0,&attr_size);std::vector<unsigned char> attributes(attr_size);si.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        HANDLE inherited[]={in,out,err};bool attributes_ok=InitializeProcThreadAttributeList(si.lpAttributeList,1,0,&attr_size)&&UpdateProcThreadAttribute(si.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof inherited,nullptr,nullptr);
        std::wstring command=quote(c.exe);for(auto& a:c.args) command+=L" "+quote(a);
        emit("\"kind\":\"launch\",\"program\":"+jw(c.exe)+",\"command_line\":"+jw(command)+",\"manifest_sha256\":"+(c.fixture?"null":j("e3f094dfe466b3e5e216a54587ce7bd966f914d480f1ae0f3995dd30dc2c0679"))+",\"verified_files\":"+verified,true);
        if(!attributes_ok||!output_ok) { close(out);close(err);close(in);if(attributes_ok) DeleteProcThreadAttributeList(si.lpAttributeList);throw std::runtime_error("Launch preflight failed"); }
        if(!SetConsoleCtrlHandler(cancel,TRUE)) {close(out);close(err);close(in);DeleteProcThreadAttributeList(si.lpAttributeList);throw std::runtime_error("Cannot install collector cancellation handler");}
        BOOL made=CreateProcessW(c.exe.c_str(),command.data(),nullptr,nullptr,TRUE,DEBUG_ONLY_THIS_PROCESS|EXTENDED_STARTUPINFO_PRESENT|(c.fixture?CREATE_NO_WINDOW:0),nullptr,c.root.c_str(),&si.StartupInfo,&pi);
        DeleteProcThreadAttributeList(si.lpAttributeList);close(out);close(err);close(in);
        if(!made) throw std::runtime_error("CreateProcessW failed");
        active=true;pid=pi.dwProcessId;primary=pi.dwThreadId;
        std::printf("observer child PID %lu\n",pid);std::fflush(stdout);
        DWORD status=DBG_CONTINUE;bool dispatched=false;
        while(active) {
          try {
            if(!pending) {
                if(!WaitForDebugEventEx(&event,250)) { if(GetLastError()!=ERROR_SEM_TIMEOUT) fail("wait_debug_event_failure");if(incomplete||cancelled) detach();continue; }
                pending=true;
                dispatched=false;
                status=event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT?DBG_EXCEPTION_NOT_HANDLED:DBG_CONTINUE;
                saved_disposition=event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT?0:DBG_CONTINUE;
            }
            if(!dispatched) {
            // Sticky before any allocation/reporting: retries never repeat resource dispatch.
            dispatched=true;
            switch(event.dwDebugEventCode) {
            case CREATE_PROCESS_DEBUG_EVENT: {
                auto& e=event.u.CreateProcessInfo;
                if(!process) {
                    if(c.failure=="alias") {close(pi.hProcess);pi.hProcess=e.hProcess;process=nullptr;}
                    else process=c.failure=="duplicate"?nullptr:dup(e.hProcess);
                    if(pi.hProcess!=e.hProcess) { if(!process) {process=pi.hProcess;pi.hProcess=nullptr;}else close(pi.hProcess); }else pi.hProcess=nullptr;
                    if(pi.hThread!=e.hThread) close(pi.hThread);else pi.hThread=nullptr;
                    add_thread(event.dwThreadId,e.hThread);
                    if(!process) fail("retained_process_duplicate_failed");
                    if(c.failure=="duplicate") fail("injected_process_duplicate_failure");
                    BOOL safety=(c.failure=="safety"||c.failure=="resident")?FALSE:DebugSetProcessKillOnExit(FALSE);
                    if(!safety) fail("kill_on_exit_setup_failed");else setup=true;
                    HANDLE f=e.hFile;e.hFile=nullptr;
                    try {add_module(reinterpret_cast<uint32_t>(e.lpBaseOfImage),f);} catch(...) {close(f);throw;}close(f);
                    emit("\"kind\":\"process_create\",\"pid\":"+std::to_string(pid)+",\"primary_tid\":"+std::to_string(primary)+",\"kill_on_exit_disabled\":"+(setup?"true":"false"));
                    if(c.failure=="throw-create"&&!failed_throw) {failed_throw=true;throw std::bad_alloc();}
                }break; }
            case CREATE_THREAD_DEBUG_EVENT: {
                add_thread(event.dwThreadId,event.u.CreateThread.hThread);
                emit("\"kind\":\"thread_create\",\"tid\":"+std::to_string(event.dwThreadId));break; }
            case EXIT_THREAD_DEBUG_EVENT: {
                emit("\"kind\":\"thread_exit\",\"tid\":"+std::to_string(event.dwThreadId));auto it=threads.find(event.dwThreadId);if(it!=threads.end()) {close(it->second);threads.erase(it);}break; }
            case LOAD_DLL_DEBUG_EVENT: { auto& e=event.u.LoadDll;HANDLE f=e.hFile;e.hFile=nullptr;try {add_module(reinterpret_cast<uint32_t>(e.lpBaseOfDll),f);} catch(...) {close(f);throw;}close(f);break; }
            case UNLOAD_DLL_DEBUG_EVENT: { uint32_t base=reinterpret_cast<uint32_t>(event.u.UnloadDll.lpBaseOfDll);emit("\"kind\":\"module_unload\",\"base\":"+hx(base));auto it=modules.find(base);if(it!=modules.end()) {close(it->second.retry);modules.erase(it);}break; }
            case EXCEPTION_DEBUG_EVENT: {
                if(event.u.Exception.ExceptionRecord.ExceptionCode==EXCEPTION_BREAKPOINT) first_bp=true;
                if(metadata_bad) retry_metadata();
                bool os=initial();
                // An unclassified initial breakpoint with missing metadata remains
                // pending while detach/attestation is serviced; never fabricate a crash.
                if(!os&&!startup&&first_exception&&event.u.Exception.dwFirstChance&&event.dwProcessId==pid&&event.u.Exception.ExceptionRecord.ExceptionCode==EXCEPTION_BREAKPOINT&&event.dwThreadId==primary) {
                    fail("startup_metadata_incomplete");dispatched=false;if(detach()) continue;Sleep(1);continue;
                }
                status=os?DBG_CONTINUE:DBG_EXCEPTION_NOT_HANDLED;saved_disposition=status;first_exception=false;if(os) startup=true;exception(os);
                if(c.failure=="cancel"&&os) cancel(CTRL_C_EVENT);break; }
            case EXIT_PROCESS_DEBUG_EVENT: exit_seen=true;exit_event=event.u.ExitProcess.dwExitCode;emit("\"kind\":\"process_exit\",\"code\":"+hx(exit_event),true);break;
            case OUTPUT_DEBUG_STRING_EVENT: emit("\"kind\":\"debug_string_skipped\",\"units\":"+std::to_string(event.u.DebugString.nDebugStringLength)+",\"skipped_bytes\":"+std::to_string(uint32_t(event.u.DebugString.nDebugStringLength)*(event.u.DebugString.fUnicode?2:1)));break;
            default: emit("\"kind\":\"other_event\",\"event\":"+std::to_string(event.dwDebugEventCode));break;
            }
            }
            // Retry failed continuation without redispatching/duplicating event resources.
            while(pending&&!continuation(status)) { if((incomplete||cancelled)&&detach()) break;Sleep(1); }
            if(exit_seen&&!pending) { active=false;break; }
            if((incomplete||cancelled)&&active) { if(cancelled) fail("cancelled");detach(); }
          } catch(...) {
            // No C++ failure may unwind an active debugging relationship.
            fail("capture_exception");output_ok=false;
            if(pending&&(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT||event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT)) metadata_bad=true;
            attention("capture_exception",ERROR_UNHANDLED_EXCEPTION,saved_disposition);
            if(pending&&event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT) close(event.u.CreateProcessInfo.hFile);
            if(pending&&event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT) close(event.u.LoadDll.hFile);
            if(pending&&event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT) {
                if(initial()) {status=DBG_CONTINUE;startup=true;first_exception=false;}
                else if(!startup&&first_exception&&event.u.Exception.dwFirstChance&&event.dwProcessId==pid&&event.u.Exception.ExceptionRecord.ExceptionCode==EXCEPTION_BREAKPOINT&&event.dwThreadId==primary) {dispatched=false;detach();Sleep(1);continue;}
            }
            if(pending) continuation(status);
            if(!pending||incomplete) detach();
            Sleep(1);
          }
        }
        // FALSE kill-on-exit is not relied on as a substitute for checked detach.
        // With failed safety+detach, the loop above remains resident and services events.
        bool waited=false;
        if(valid(process)) { DWORD w;do { w=WaitForSingleObject(process,250); }while(w==WAIT_TIMEOUT);waited=w==WAIT_OBJECT_0&&GetExitCodeProcess(process,&exit_code);if(!waited) fail("retained_wait_failed"); }
        else fail("retained_handle_missing");
        if(!exit_seen||!waited||exit_event!=exit_code) fail(detached?"detached_incomplete":"exit_observation_mismatch");
        for(auto& [id,t]:threads) { (void)id;close(t); }threads.clear();for(auto& [base,m]:modules) { (void)base;close(m.retry); }modules.clear();
        close(process);close(pi.hProcess);close(pi.hThread);close(log);SetConsoleCtrlHandler(cancel,FALSE);
        std::string summary="{\"collector_success\":"+std::string(incomplete?"false":"true")+",\"incomplete\":"+(incomplete?"true":"false")+",\"reason\":"+j(reason)+",\"pid\":"+std::to_string(pid)+",\"primary_tid\":"+std::to_string(primary)+",\"startup_consumed\":"+(startup?"true":"false")+",\"detached\":"+(detached?"true":"false")+",\"detach_attempts\":"+std::to_string(detach_attempts)+",\"pending_detach_attempts\":"+std::to_string(detach_pending)+",\"attention_count\":"+std::to_string(attention_count)+",\"exit_event_seen\":"+(exit_seen?"true":"false")+",\"wait_completed\":"+(waited?"true":"false")+",\"exit_code_unsigned\":"+(waited?std::to_string(exit_code):"null")+",\"exit_code_signed\":"+(waited?std::to_string(static_cast<int32_t>(exit_code)):"null")+",\"exit_code_hex\":"+(waited?hx(exit_code):"null")+",\"event_bytes\":"+std::to_string(written)+",\"stdout_sha256\":"+(waited?j(sha(c.prefix+L".stdout.log")):"null")+",\"stderr_sha256\":"+(waited?j(sha(c.prefix+L".stderr.log")):"null")+"}\n";
        HANDLE receipt=CreateFileW((c.prefix+L".summary.json").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,0,nullptr);DWORD got=0;
        if(!valid(receipt)||!WriteFile(receipt,summary.data(),DWORD(summary.size()),&got,nullptr)||got!=summary.size()||!FlushFileBuffers(receipt)) { close(receipt);throw std::runtime_error("Summary output failed after safe detach/exit"); }close(receipt);
    }
};
}
int wmain(int argc,wchar_t** argv) {
    try {
        // Dry validation of owned fixture bytes exercises the production file gate.
        // This path never creates a process, even when the supplied values match.
        if(argc==5&&std::wstring(argv[1])==L"--fixture-file-check") {verify_file(full(argv[2]),std::stoull(argv[3]),utf8(argv[4]));return 0;}
        Observer o; o.c.root=full(L".");
        for(int i=1;i<argc;++i) {
            std::wstring a=argv[i];
            if(a==L"--") { for(++i;i<argc;++i) o.c.args.emplace_back(argv[i]);break; }
            if(++i>=argc) throw std::runtime_error("Missing option value");
            if(a==L"--output") o.c.prefix=full(argv[i]);
            else if(a==L"--game-root") o.c.root=full(argv[i]);
            else if(a==L"--fixture") {o.c.fixture=true;o.c.exe=full(argv[i]);}
            else if(a==L"--fixture-failure") o.c.failure=utf8(argv[i]);
            else throw std::runtime_error("Unknown option");
        }
        auto local=o.c.root+L"\\local\\";
        if(o.c.prefix.empty()||o.c.prefix.size()<=local.size()||_wcsnicmp(o.c.prefix.c_str(),local.c_str(),local.size())) throw std::runtime_error("Output must be under repository local/");
        if(!o.c.fixture) {
            throw std::runtime_error("Historical R1 game capture is retired; this tool supports owned fixtures only");
        }
        o.run();return o.incomplete?1:0;
    } catch(const std::exception& e) { std::fprintf(stderr,"observer: %s\n",e.what());return 2; }
}
