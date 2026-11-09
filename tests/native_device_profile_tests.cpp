#include "platform/native_device_profile.h"
#include "platform/guest_extent.h"
#include "abi/guest_hook.h"
#include "audio/opensl.h"
#include "bionic/runtime.h"
#include "gles/runtime.h"
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {
uint32_t cpu_address=0,mem_address=0,exit_address=0;
uint32_t captured_cpu=0,captured_mem=0,captured=0;
// Test-only terminal seam: the first native log precedes device-name strings or
// profile application. Record actual native stores, then use the existing native
// epilogue. No host call, fabricated profile result or native allocation occurs.
__declspec(naked) void __cdecl capture_first_log() {
    __asm {
        mov eax,dword ptr [cpu_address]
        mov ecx,dword ptr [eax]
        mov dword ptr [captured_cpu],ecx
        mov eax,dword ptr [mem_address]
        mov ecx,dword ptr [eax]
        mov dword ptr [captured_mem],ecx
        mov dword ptr [captured],1
        add esp,4
        jmp dword ptr [exit_address]
    }
}
void check(bool condition,const char *message) { if(!condition) throw std::runtime_error(message); }
uint32_t &word(uint32_t address) { return *reinterpret_cast<uint32_t *>(address); }
constexpr auto owner="_ZN13DeviceOptions11LoadProfileEv";
constexpr auto logger="_ZN3glf7Console7PrintlnEPKcz";
void matrix(const nova::ElfImage &image,bool independent) {
    const auto function=nova::abi::require_function(image,owner);
    const auto width=image.symbol("g_ScreenW");
    check(width!=0,"native screen width is mapped");
    exit_address=function+0x1ad;
    nova::abi::GuestCall terminal(image,owner,0x32,logger,reinterpret_cast<uint32_t>(&capture_first_log));
    std::array<uint32_t,4> unused_owner{};
    for(const auto extent:nova::platform::standard_render_extents) for(auto cpu:{0u,1u,2u}) for(auto mem:{0u,1u,2u}) {
        const auto screen=static_cast<uint32_t>(extent.width);
        word(width)=screen; word(cpu_address)=cpu; word(mem_address)=mem; captured=0;
        nova::abi::invoke(function,{reinterpret_cast<uint32_t>(unused_owner.data())});
        const bool overridden=screen==1920 && !independent;
        check(captured==1 && captured_cpu==(overridden ? 1u : cpu) && captured_mem==(overridden ? 1u : mem),
              "actual native selector matrix preserves caller pair except original1920 detour");
        check(word(width)==screen,"compatibility policy never fabricates a screen width");
    }
}
void mutate_byte(uint32_t address,uint8_t value) {
    auto *target=reinterpret_cast<uint8_t *>(address);
    DWORD protection=0,ignored=0;
    check(VirtualProtect(target,1,PAGE_EXECUTE_READWRITE,&protection)!=FALSE,"test mutation protection");
    *target=value;
    check(VirtualProtect(target,1,protection,&ignored)!=FALSE && FlushInstructionCache(GetCurrentProcess(),target,1),
          "test mutation restore/cache");
}
class LocalVariant {
  public:
    LocalVariant(const std::filesystem::path &directory,std::span<const uint8_t> bytes) {
        // CTest supplies an ignored build runtime directory. Never alter the input SO.
        std::filesystem::create_directories(directory);
        wchar_t name[MAX_PATH]{};
        check(GetTempFileNameW(directory.c_str(),L"T11",0,name)!=0,"create unique local variant");
        path_=name;
        try {
            std::ofstream out(path_,std::ios::binary|std::ios::trunc);
            out.write(reinterpret_cast<const char *>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
            out.close();
            check(out.good(),"write complete local variant");
        } catch(...) { remove(); throw; }
    }
    ~LocalVariant() { remove(); }
    LocalVariant(const LocalVariant &)=delete;
    LocalVariant &operator=(const LocalVariant &)=delete;
    const std::filesystem::path &path() const { return path_; }
  private:
    void remove() { std::error_code ignored; std::filesystem::remove(path_,ignored); }
    std::filesystem::path path_;
};
uint32_t source_offset(std::span<const uint8_t> bytes,uint32_t vaddr) {
    nova::elf::Header header{};
    check(bytes.size()>=sizeof header,"variant ELF header exists");
    std::memcpy(&header,bytes.data(),sizeof header);
    for(uint32_t i=0;i<header.phnum;++i) {
        nova::elf::Program program{};
        const auto at=static_cast<size_t>(header.phoff)+i*sizeof program;
        check(at<=bytes.size() && sizeof program<=bytes.size()-at,"variant program header exists");
        std::memcpy(&program,bytes.data()+at,sizeof program);
        if(program.type!=nova::elf::pt_load || vaddr<program.vaddr) continue;
        const auto delta=vaddr-program.vaddr;
        if(delta>program.filesz || sizeof(uint32_t)>program.filesz-delta) continue;
        const auto offset=uint64_t(program.offset)+delta;
        check(offset<=bytes.size() && sizeof(uint32_t)<=bytes.size()-offset,"variant global is file backed");
        return static_cast<uint32_t>(offset);
    }
    throw std::runtime_error("variant global has no source offset");
}
void reject_swapped_image(const nova::ElfImage &accepted,const nova::ImportRegistry &imports,
                          const std::filesystem::path &accepted_path,const std::filesystem::path &directory) {
    const auto source=accepted.source_bytes();
    std::vector<uint8_t> changed(source.begin(),source.end());
    // Change a real initialized selector outside LoadProfile, leaving its entire
    // function and RVA intact. A body-only check would accept the wrong image.
    changed[source_offset(source,0x11438fc)]^=1;
    LocalVariant file(directory,changed);
    nova::ElfImage swapped(file.path(),imports);
    const auto original_function=nova::abi::require_function(accepted,owner);
    const auto swapped_function=nova::abi::require_function(swapped,owner);
    check(!std::memcmp(reinterpret_cast<const void *>(original_function),reinterpret_cast<const void *>(swapped_function),925),
          "swapped identity retains the complete925-byte function");
    check(word(accepted.address(0x11438fc,4))!=word(swapped.address(0x11438fc,4)),
          "swapped identity changes mapped selector data outside the function");
    std::array<uint8_t,925> before{};
    std::memcpy(before.data(),reinterpret_cast<const void *>(swapped_function),before.size());
    bool rejected=false;
    try { nova::platform::NativeDeviceProfile wrong(swapped,accepted_path); }
    catch(const std::runtime_error &error) {
        rejected=std::strcmp(error.what(),"Native profile loaded image identity mismatch")==0;
    }
    check(rejected && !std::memcmp(before.data(),reinterpret_cast<const void *>(swapped_function),before.size()),
          "accepted path cannot admit a different loaded image or mutate its body");
}
}
int main(int argc,char **argv) {
    if(argc!=3) return 2;
    if(!std::filesystem::exists(argv[1])) return 77;
    try {
        nova::ImportRegistry imports;
        nova::bionic::register_boot(imports); nova::audio::register_opensl(imports); nova::gles::register_imports(imports);
        nova::bionic::CxaRegistry cxa(imports);
        nova::ElfImage image(argv[1],imports);
        { nova::platform::NativeDeviceProfile authenticated(image,argv[1]); }
        reject_swapped_image(image,imports,argv[1],argv[2]);
        const auto function=nova::abi::require_function(image,owner);
        cpu_address=image.address(0x11438fc,4); mem_address=image.address(0x1143900,4);
        const auto width=image.symbol("g_ScreenW");
        const auto saved_cpu=word(cpu_address),saved_mem=word(mem_address),saved_width=word(width);
        std::array<uint8_t,925> original{};
        std::memcpy(original.data(),reinterpret_cast<void *>(function),original.size());
        matrix(image,false); // Positive negative-control witness of the native1920 override.
        {
            nova::platform::NativeDeviceProfile policy(image,argv[1]);
            check(policy.applied(),"authenticated profile compatibility patch is installed");
            bool duplicate_rejected=false;
            try { nova::platform::NativeDeviceProfile duplicate(image,argv[1]); }
            catch(const std::runtime_error &) { duplicate_rejected=true; }
            check(duplicate_rejected,"second installer cannot acquire an already owned branch");
            matrix(image,true);
        }
        check(!std::memcmp(original.data(),reinterpret_cast<void *>(function),original.size()),
              "destruction restores the entire925-byte body byte-exact");
        matrix(image,false); // Restored1920 must again choose1/1 for every caller pair.
        bool rejected=false;
        try { nova::platform::NativeDeviceProfile wrong(image,std::filesystem::path(argv[1]).parent_path()/"t11d-absent.so"); }
        catch(const std::runtime_error &) { rejected=true; }
        check(rejected && !std::memcmp(original.data(),reinterpret_cast<void *>(function),original.size()),
              "unavailable source identity fails without code mutation");
        const auto foreign=function+0x25;
        const auto old=*reinterpret_cast<const uint8_t *>(foreign);
        mutate_byte(foreign,static_cast<uint8_t>(old^1));
        rejected=false;
        try { nova::platform::NativeDeviceProfile wrong(image,argv[1]); }
        catch(const std::runtime_error &) { rejected=true; }
        const bool foreign_preserved=*reinterpret_cast<const uint8_t *>(foreign)==static_cast<uint8_t>(old^1);
        mutate_byte(foreign,old);
        check(rejected && foreign_preserved,"foreign native body is rejected without overwrite");
        word(cpu_address)=saved_cpu; word(mem_address)=saved_mem; word(width)=saved_width;
        std::puts("native profile: standard16:9 catalogue caller CPU/MEM matrix, admission and cleanup passed");
        return 0;
    } catch(const std::exception &error) {
        std::fprintf(stderr,"native profile: %s\n",error.what()); return 1;
    }
}
