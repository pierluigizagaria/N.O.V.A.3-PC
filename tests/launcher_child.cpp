#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
int wmain(int argc,wchar_t **argv) {
    const auto root=std::filesystem::current_path();
    if(argc!=8 || std::filesystem::path(argv[1]).generic_string()!="local/input/lib/x86/libNOVA3_neon.so" ||
       std::wstring(argv[2])!=L"--profile" || std::wstring(argv[3])!=L"player" ||
       std::wstring(argv[4])!=L"--profile-root" || std::filesystem::path(argv[5]).generic_string()!="local/profiles/player" ||
       std::wstring(argv[6])!=L"--obb-dir" || std::filesystem::path(argv[7]).generic_string()!="local/game/obb") return 31;
    if(GetConsoleWindow()) return 32;
    std::ofstream(root/L"child-started.txt")<<"synthetic host only";
    DWORD done=0; const char output[]="synthetic host stdout\n";
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),output,sizeof(output)-1,&done,nullptr);
    if(done!=sizeof(output)-1) return 33;
    return std::filesystem::exists(root/L"fail-child")?7:0;
}
