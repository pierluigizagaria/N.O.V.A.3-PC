#include "platform/fov_config.h"
#include "input/bindings.h"
#include "platform/profile.h"
#include <bit>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
using namespace nova::platform;
namespace {
void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
std::string read(const std::filesystem::path &path) {
    std::ifstream file(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(file),{}};
}
void write(const std::filesystem::path &path,std::string_view bytes) {
    std::ofstream file(path,std::ios::binary|std::ios::trunc);
    file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    check(bool(file),"isolated test file write");
}
enum class Fault { none,create,write,flush,close,replace,remove,external };
Fault fault=Fault::none;
HANDLE temp_handle=INVALID_HANDLE_VALUE;
std::filesystem::path temp_path,config_path;
unsigned removed=0;
HANDLE WINAPI open_file(LPCWSTR path,DWORD access,DWORD sharing,LPSECURITY_ATTRIBUTES security,
                       DWORD disposition,DWORD attributes,HANDLE model) {
    if(disposition==CREATE_NEW) {
        temp_path=path;
        if(fault==Fault::create) { SetLastError(ERROR_FILE_EXISTS); return INVALID_HANDLE_VALUE; }
    }
    auto handle=CreateFileW(path,access,sharing,security,disposition,attributes,model);
    if(disposition==CREATE_NEW) temp_handle=handle;
    return handle;
}
BOOL WINAPI write_file(HANDLE file,LPCVOID bytes,DWORD length,LPDWORD written,LPOVERLAPPED overlapped) {
    if(fault==Fault::write) { *written=0; SetLastError(ERROR_DISK_FULL); return FALSE; }
    return WriteFile(file,bytes,length,written,overlapped);
}
BOOL WINAPI flush_file(HANDLE file) {
    if(fault==Fault::flush || fault==Fault::remove) { SetLastError(ERROR_WRITE_FAULT); return FALSE; }
    if(fault==Fault::external) write(config_path,"language = external\nworld_fov = native\n");
    return FlushFileBuffers(file);
}
BOOL WINAPI close_file(HANDLE file) {
    const bool fail=fault==Fault::close && file==temp_handle;
    auto result=CloseHandle(file);
    if(fail) { SetLastError(ERROR_WRITE_FAULT); return FALSE; }
    return result;
}
BOOL WINAPI replace_file(LPCWSTR target,LPCWSTR replacement,LPCWSTR backup,DWORD flags,
                         LPVOID excluded,LPVOID reserved) {
    if(fault==Fault::replace) { SetLastError(ERROR_SHARING_VIOLATION); return FALSE; }
    return ReplaceFileW(target,replacement,backup,flags,excluded,reserved);
}
BOOL WINAPI move_file(LPCWSTR source,LPCWSTR target,DWORD flags) {
    if(fault==Fault::replace) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return MoveFileExW(source,target,flags);
}
BOOL WINAPI remove_file(LPCWSTR path) {
    ++removed;
    if(fault==Fault::remove) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return DeleteFileW(path);
}
}
int main(int argc,char **argv) {
    try {
        check(argc==2,"isolated test directory required");
        const std::filesystem::path directory=argv[1];
        std::filesystem::create_directories(directory);
        config_path=directory/"nova3pc.conf";
        const FovOptions pair{std::bit_cast<float>(0x42b40001u),104.f};
        const std::string original="# untouched\r\nlanguage = en\r\n world_fov\t= native  \r\n"
            "world_fov = 70\nweapon_fov=100\r\naudio = null";
        auto merged=merge_fov_config(original,pair);
        check(merged.find("# untouched\r\nlanguage = en\r\n")==0,"unrelated bytes and CRLF exact");
        check(merged.find("audio = null")!=std::string::npos,"last unrelated unterminated line exact");
        check(merged.find("native")==std::string::npos && merged.find("world_fov = 70")==std::string::npos,
              "all duplicate FOV occurrences replaced");
        write(config_path,merged);
        std::filesystem::path obb;
        auto parsed=nova::profile::read_config(config_path,obb);
        check(std::bit_cast<uint32_t>(*parsed.world_fov)==std::bit_cast<uint32_t>(*pair.world) &&
              parsed.weapon_fov==pair.weapon,"production parser float bit round-trip");
        check(merge_fov_config(merged,pair)==merged,"idempotent text merge");
        const std::string legacy="world_fov=120\nweapon_fov=124\n";
        write(config_path,legacy); parsed=nova::profile::read_config(config_path,obb);
        check(parsed.world_fov==120.f && parsed.weapon_fov==104.f && read(config_path)==legacy,
              "legacy weapon above104 clamps without changing world or writing config");
        check(merge_fov_config(legacy,{parsed.world_fov,parsed.weapon_fov})==
              "world_fov=120\nweapon_fov=104\n","normal FOV save persists effective104");
        for(const auto &input : {std::string{},std::string("# hello"),std::string("language=en\n"),
                                std::string("world_fov=60\r\nweapon_fov=130")}) {
            auto text=merge_fov_config(input,{});
            write(config_path,text); parsed=nova::profile::read_config(config_path,obb);
            check(!parsed.world_fov && !parsed.weapon_fov,"Native merge round-trip");
        }
        for(auto bad : {"unknown=x","world_fov=nan","world_fov=59.9","weapon_fov=131",
                        "audio=bad","missing equals","world_fov=90 # unsupported inline comment"}) {
            bool rejected=false;
            try { (void)merge_fov_config(bad,pair); } catch(const std::exception &) { rejected=true; }
            check(rejected,"unmergeable content rejected");
        }
        for(FovOptions invalid : {FovOptions{std::numeric_limits<float>::infinity(),100.f},
                                  FovOptions{90.f,std::numeric_limits<float>::quiet_NaN()},
                                  FovOptions{90.f,104.001f}}) {
            bool rejected=false;
            try { (void)merge_fov_config(original,invalid); } catch(const std::exception &) { rejected=true; }
            check(rejected,"invalid visible pair rejected atomically");
        }
        FovConfigIo io;
        io.open=open_file; io.write=write_file; io.flush=flush_file; io.close=close_file;
        io.replace=replace_file; io.move=move_file; io.remove=remove_file;
#include "weapon_position_config_cases.inc"
        for(bool existing : {false,true}) for(auto injected : {Fault::none,Fault::create,Fault::write,
                Fault::flush,Fault::close,Fault::replace,Fault::remove,Fault::external}) {
            fault=Fault::none;
            std::filesystem::remove(config_path);
            if(existing) write(config_path,original);
            temp_path.clear(); temp_handle=INVALID_HANDLE_VALUE; removed=0;
            fault=injected;
            auto result=save_fov_config(config_path,pair,91,&io);
            check(result.visible_revision==91,"Save retains exact visible revision before render ack");
            if(injected==Fault::none) {
                check(result.saved && result.error.empty(),"explicit Save success");
                parsed=nova::profile::read_config(config_path,obb);
                check(parsed.world_fov==pair.world && parsed.weapon_fov==pair.weapon,"explicit Save exact visible pair");
            } else {
                check(!result.saved && !result.error.empty(),"injected failure visible");
                if(injected!=Fault::external)
                    check(existing ? read(config_path)==original : !std::filesystem::exists(config_path),
                          "failed Save preserves previous file or absence");
                else check(read(config_path).find("external")!=std::string::npos,"concurrent edit not overwritten");
                if(injected==Fault::create) check(!removed,"collision never deletes unowned temp");
                else if(injected==Fault::remove) {
                    check(std::filesystem::exists(temp_path) && result.error.find("clean owned")!=std::string::npos,
                          "cleanup failure reported honestly");
                    std::filesystem::remove(temp_path);
                } else check(!std::filesystem::exists(temp_path),"owned temporary cleaned after failure");
            }
        }
        fault=Fault::none;
        const DisplaySettings display{true,{1920,1080},{1280,720}};
        const std::string video_original="# video\r\n fullscreen = broken  \r\nfullscreen=false\n"
            "fullscreen_resolution=999999x0\r\nwindow_size=nonsense\r\nworld_fov=90\nweapon_fov=native\n";
        auto video=merge_display_config(video_original,display);
        check(video.find("# video\r\n fullscreen = true  \r\nfullscreen=true\n")==0,
              "Video merge repairs all duplicate invalid preferences and preserves comments/spacing/newlines");
        check(video.find("world_fov=90\nweapon_fov=native\n")!=std::string::npos,"Display save preserves optics bytes");
        write(config_path,video); parsed=nova::profile::read_config(config_path,obb);
        check(parsed.display==display,"Display settings production parser round-trip");
        check(merge_display_config(video,display)==video,"Display merge is idempotent");
        check(merge_fov_config(video,pair).find("fullscreen_resolution=1920x1080")!=std::string::npos,
              "Later FOV save accepts and preserves video selection");
        write(config_path,"fullscreen=invalid\nfullscreen_resolution=0x0\nwindow_size=nan\n");
        parsed=nova::profile::read_config(config_path,obb);
        check(parsed.display==DisplaySettings{},"Malformed video preferences safely default without aborting startup");
        for(auto injected:{Fault::none,Fault::create,Fault::write,Fault::flush,Fault::close,Fault::replace,Fault::external}) {
            fault=Fault::none; write(config_path,video_original); temp_handle=INVALID_HANDLE_VALUE;
            fault=injected; auto result=save_display_config(config_path,display,&io);
            check(result.saved==(injected==Fault::none),"Display uses same atomic transaction and visible I/O failure");
            if(injected!=Fault::none && injected!=Fault::external)
                check(read(config_path)==video_original,"Failed display save preserves original config byte-exact");
            if(injected==Fault::external) check(read(config_path).find("external")!=std::string::npos,"Display save never overwrites concurrent edit");
        }
        fault=Fault::none;
        write(config_path,"language=external-valid\n");
        auto saved=save_fov_config(config_path,{},92,&io);
        check(saved.saved && read(config_path).find("external-valid")!=std::string::npos,"Save merges current valid external edits");
        write(config_path,"unknown=external-invalid\n");
        saved=save_fov_config(config_path,pair,93,&io);
        check(!saved.saved && read(config_path)=="unknown=external-invalid\n","invalid external edit retained");
        const auto render_original=std::string("# render\r\nrender_resolution = invalid  \r\nrender_resolution=1280x720\n")+video;
        const auto render=merge_render_config(render_original,{1600,900});
        check(render.find("render_resolution = 1600x900  \r\nrender_resolution=1600x900\n")!=std::string::npos,
              "Render save repairs all duplicate owned keys with exact surrounding bytes");
        write(config_path,render); parsed=nova::profile::read_config(config_path,obb);
        check(parsed.render_extent==GuestExtent{1600,900} && parsed.display==display,
              "Actual native render setting is distinct from output display preferences");
        check(merge_render_config(render,{1600,900})==render,"Render save is idempotent");
        check(merge_fov_config(render,pair).find("render_resolution=1600x900")!=std::string::npos &&
              merge_display_config(render,display).find("render_resolution=1600x900")!=std::string::npos,
              "FOV and fullscreen transactions preserve native render selection");
        const auto full_hd=merge_render_config(render,{1920,1080});
        write(config_path,full_hd); parsed=nova::profile::read_config(config_path,obb);
        check(parsed.render_extent==GuestExtent{1920,1080},"Full HD persists through the actual config parser");
        for(const auto extent:standard_render_extents) {
            write(config_path,merge_render_config(render,extent));
            check(nova::profile::read_config(config_path,obb).render_extent==extent,"entire admitted native catalogue persists without output changes");
        }
        for(auto invalid:{"7680x4320","1920x1200","1600x720","invalid","0x0"}) {
            write(config_path,std::string("render_resolution=")+invalid+"\n");
            check(nova::profile::read_config(config_path,obb).render_extent==GuestExtent{},
                  "Unsupported render requests fall back to complete1280 pair");
        }
        for(auto injected:{Fault::none,Fault::create,Fault::write,Fault::flush,Fault::close,Fault::replace,Fault::external}) {
            fault=Fault::none; write(config_path,video_original); temp_handle=INVALID_HANDLE_VALUE;
            fault=injected; const auto result=save_render_config(config_path,{1600,900},&io);
            check(result.saved==(injected==Fault::none),"Render setting uses the shared atomic transaction");
            if(injected!=Fault::none && injected!=Fault::external)
                check(read(config_path)==video_original,"Failed render save preserves previous config");
            if(injected==Fault::external) check(read(config_path).find("external")!=std::string::npos,
                                               "Render save preserves concurrent changes");
        }
        fault=Fault::none;
        std::filesystem::remove(config_path);
        // O07: the separate controls file reuses the same transactional writer.
        const auto host_config=config_path;
        config_path=directory/"controls.conf";
        const std::string controls_original="# keep\r\nsensitivity=.3\r\ninvert_y=true\r\nbind.Z=jump\r\n";
        nova::input::ControlsConfig bindings;
        check(nova::input::rebind(bindings,nova::input::Action::jump,
              {nova::input::BindingKind::mouse,SDL_BUTTON_X1}).empty(),"prepare control mapping");
        for(auto injected:{Fault::none,Fault::create,Fault::write,Fault::flush,Fault::close,Fault::replace,Fault::external}) {
            fault=Fault::none; write(config_path,controls_original); temp_handle=INVALID_HANDLE_VALUE;
            fault=injected; const auto result=save_controls_config(config_path,bindings,&io);
            check(result.saved==(injected==Fault::none),"Controls uses shared atomic save and exposes failure");
            if(injected==Fault::none) {
                const auto restored=nova::input::ControlsConfig::read(config_path);
                check(nova::input::same_bindings(restored,bindings) && restored.sensitivity==.3f &&
                      restored.invert_y,"Controls saved map and unrelated scalar settings round-trip");
            } else if(injected!=Fault::external)
                check(read(config_path)==controls_original,"Controls failure preserves destination byte-exact");
            else check(read(config_path).find("external")!=std::string::npos,"Controls save preserves concurrent edit");
        }
        bindings.sensitivity=.4f; bindings.aim_multiplier=.07f;
        for(auto injected:{Fault::none,Fault::write,Fault::flush,Fault::close,Fault::replace,Fault::external}) {
            fault=Fault::none; write(config_path,controls_original); temp_handle=INVALID_HANDLE_VALUE;
            fault=injected;
            const auto result=save_controls_preferences(config_path,bindings,true,true,&io);
            check(result.saved==(injected==Fault::none),"combined mapping/sensitivity edit is one atomic transaction");
            if(injected==Fault::none) {
                const auto restored=nova::input::ControlsConfig::read(config_path);
                check(nova::input::same_bindings(restored,bindings) && restored.sensitivity==.4f &&
                      restored.aim_multiplier==.07f && restored.invert_y,"complete retry saves independent values and mappings");
            } else if(injected!=Fault::external)
                check(read(config_path)==controls_original,"failed sensitivity transaction cannot save half a pair");
            else check(read(config_path).find("external")!=std::string::npos,"sensitivity save respects concurrent edits");
        }
        fault=Fault::none; write(config_path,controls_original);
        check(save_controls_preferences(config_path,bindings,false,true,&io).saved,
              "sensitivity-only retry succeeds explicitly");
        check(read(config_path).find("bind.Z=jump\r\n")!=std::string::npos,
              "sensitivity-only save preserves original aliases byte-exact");
        fault=Fault::none; std::filesystem::remove(config_path);
        config_path=host_config;
        const std::string ui_original="# keep UI\r\nui_scale=100\r\nui_scale = 75\nworld_fov=native\n";
        for(int value:{50,100,150,300}) {
            for(auto injected:{Fault::none,Fault::write,Fault::flush,Fault::close,Fault::replace,Fault::external}) {
                fault=Fault::none; write(config_path,ui_original); fault=injected;
                const auto result=save_ui_scale_config(config_path,value,&io);
                check(result.saved==(injected==Fault::none),"UI scale transactional save reports actual result");
                if(injected==Fault::none) {
                    check(nova::profile::read_config(config_path,obb).ui_scale==value,"UI percentage round trips through production parser");
                    check(read(config_path)=="# keep UI\r\nui_scale="+std::to_string(value)+"\r\nui_scale = "+std::to_string(value)+"\nworld_fov=native\n","UI duplicates updated and unrelated bytes exact");
                } else if(injected!=Fault::external) check(read(config_path)==ui_original,"failed UI save preserves config");
                else check(read(config_path).find("external")!=std::string::npos,"UI save preserves external edits");
            }
        }
        fault=Fault::none; write(config_path,"language=en\n");
        check(nova::profile::read_config(config_path,obb).ui_scale==100,"missing UI scale defaults to100 without writing");
        check(read(config_path)=="language=en\n","default UI config load is read only");
        for(const auto invalid:{"49","301","100.5","nan","-1","100garbage"}) {
            write(config_path,std::string("ui_scale=")+invalid+"\n"); bool rejected=false;
            try { (void)nova::profile::read_config(config_path,obb); } catch(...) { rejected=true; }
            check(rejected,"malformed UI scale rejected by config loader");
        }

        std::filesystem::remove(config_path);
        check(std::filesystem::is_empty(directory),"no config temp, ImGui INI or log remains");
        std::puts("fov config: exact visible pair, parser bits, duplicates, external edits and I/O failures passed");
        return 0;
    } catch(const std::exception &error) { std::fprintf(stderr,"FOV config test: %s\n",error.what()); return 1; }
}
