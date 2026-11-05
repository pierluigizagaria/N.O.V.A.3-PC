#include "launcher/process.h"
#include <vector>
namespace nova::launcher {
HANDLE start_game(const Layout &layout,const fs::path &executable) {
    if(!fs::is_regular_file(executable))
        throw std::runtime_error(executable.filename().string()+" is missing. Keep the launcher beside the game executable and its DLLs.");
    const auto logs=layout.root/"local/logs/launcher";
    // Do not redirect game output through a junction outside the installation.
    auto check=layout.root;
    for(const auto &part:fs::path("local/logs/launcher")) {
        check/=part;
        const auto flags=GetFileAttributesW(check.c_str());
        if(flags!=INVALID_FILE_ATTRIBUTES && (flags&FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("The launcher log folder must not contain links or junctions.");
    }
    fs::create_directories(logs);
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    const auto log_path=logs/("game-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+".log");
    HANDLE log=CreateFileW(log_path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr);
    struct Close { HANDLE h; ~Close(){if(h!=INVALID_HANDLE_VALUE) CloseHandle(h);} } close_log{log},close_input{input};
    if(log==INVALID_HANDLE_VALUE || input==INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot create the game startup log.");
    SIZE_T bytes=0; InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
    std::vector<unsigned char> storage(bytes);
    auto attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if(!InitializeProcThreadAttributeList(attributes,1,0,&bytes))
        throw std::runtime_error("Cannot initialize game process attributes.");
    struct Delete { LPPROC_THREAD_ATTRIBUTE_LIST p; ~Delete(){DeleteProcThreadAttributeList(p);} } cleanup{attributes};
    HANDLE handles[]{log,input};
    if(!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr))
        throw std::runtime_error("Cannot isolate inherited game handles.");
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb=sizeof(startup);
    startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput=input; startup.StartupInfo.hStdOutput=log; startup.StartupInfo.hStdError=log;
    startup.lpAttributeList=attributes;
    PROCESS_INFORMATION process{}; auto command=game_arguments(layout,executable);
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,
        CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,layout.root.c_str(),&startup.StartupInfo,&process))
        throw std::runtime_error("Cannot start the game (Windows error "+std::to_string(GetLastError())+"). Check the runtime DLLs.");
    CloseHandle(process.hThread); return process.hProcess;
}
}
