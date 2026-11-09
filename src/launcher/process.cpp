#include "launcher/process.h"
#include <algorithm>
#include <regex>
#include <vector>
namespace nova::launcher {
namespace {
// Retain two closed sessions plus the new one. Never follow links or remove
// unrelated files; Windows refuses deletion of a log held by a running game.
void prune_game_logs(const fs::path &directory) {
    std::vector<std::pair<fs::file_time_type,fs::path>> logs;
    const std::wregex name(L"game-[0-9]+-[0-9]+\\.log");
    std::error_code error;
    for(const auto &entry:fs::directory_iterator(directory,error)) {
        const auto flags=GetFileAttributesW(entry.path().c_str());
        if(flags==INVALID_FILE_ATTRIBUTES || (flags&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) ||
           !std::regex_match(entry.path().filename().wstring(),name)) continue;
        const auto time=entry.last_write_time(error);
        if(!error) logs.emplace_back(time,entry.path());
        error.clear();
    }
    std::sort(logs.begin(),logs.end(),[](const auto &a,const auto &b){return a.first>b.first;});
    for(size_t i=2;i<logs.size();++i) DeleteFileW(logs[i].second.c_str());
}
}
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
    prune_game_logs(logs);
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
