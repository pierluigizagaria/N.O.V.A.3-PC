// Transactional host settings. No lifecycle path invokes Save automatically.
#include "platform/fov_config.h"
#include "input/bindings.h"
#include <sstream>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <windows.h>

namespace nova::platform {
namespace {
std::string_view trim(std::string_view value) {
    // Match the existing profile parser's ASCII whitespace domain.
    auto whitespace=[](char c) { return c==' ' || c=='\t' || c=='\r' || c=='\n'; };
    while (!value.empty() && whitespace(value.front())) value.remove_prefix(1);
    while (!value.empty() && whitespace(value.back())) value.remove_suffix(1);
    return value;
}
void validate_fov(std::optional<float> value, float maximum) {
    if (value && (!std::isfinite(*value) || *value<60 || *value>maximum))
        throw std::invalid_argument("FOV value is outside its accepted range");
}
std::string format(std::optional<float> value) {
    if (!value) return "native";
    std::array<char,64> buffer{};
    auto result=std::to_chars(buffer.data(),buffer.data()+buffer.size(),*value,
        std::chars_format::general,std::numeric_limits<float>::max_digits10);
    if (result.ec!=std::errc{}) throw std::runtime_error("Cannot format FOV value");
    return {buffer.data(),result.ptr};
}
void validate_line(std::string_view key,std::string_view value) {
    if (key=="world_fov" || key=="weapon_fov") {
        if (value=="native") return;
        float number=0;
        auto result=std::from_chars(value.data(),value.data()+value.size(),number,
                                    std::chars_format::general);
        if (result.ec!=std::errc{} || result.ptr!=value.data()+value.size())
            throw std::runtime_error("Current config contains an invalid FOV value");
        // Legacy weapon values remain readable until an ordinary FOV save repairs them.
        validate_fov(number,key=="world_fov" ? 120.f : 130.f);
    } else if (key=="lod_distance" || key=="texture_filtering") {
        int number=-1;
        const auto result=std::from_chars(value.data(),value.data()+value.size(),number);
        const bool valid=key=="lod_distance" ? number>=1 && number<=4 : number>=0 && number<=2;
        if(result.ec!=std::errc{} || result.ptr!=value.data()+value.size() || !valid)
            throw std::runtime_error("Current config contains invalid graphics quality");
    } else if (key=="ui_scale") {
        if(!ui::parse_percent(value)) throw std::runtime_error("Current config contains an invalid UI scale percentage");
    } else if (key=="movement_wobble") {
        if(!movement_wobble::parse_strength(value))
            throw std::runtime_error("Current config contains an invalid movement_wobble percentage");
    } else if (key=="fullscreen" || key=="fullscreen_resolution" || key=="window_size" || key=="render_resolution") {
        // Invalid saved video values are intentionally repairable by Display.
        return;
    } else if (key=="weapon_position_x" || key=="weapon_position_y") {
        if(!weapon_placement::parse_coordinate(value))
            throw std::runtime_error("Current config contains an invalid weapon position");
    } else if (key=="audio") {
        if (value!="sdl" && value!="null")
            throw std::runtime_error("Current config contains invalid audio selection");
    } else if (key!="obb_directory" && key!="graphics_backend" && key!="language" &&
               !key.starts_with("hosts.")) {
        throw std::runtime_error("Current config contains an unknown key");
    }
}
struct Handle {
    const FovConfigIo &io;
    HANDLE value=INVALID_HANDLE_VALUE;
    Handle(const FovConfigIo &api,HANDLE input):io(api),value(input) {}
    Handle(const Handle &)=delete;
    ~Handle() { if (value!=INVALID_HANDLE_VALUE) io.close(value); }
    bool close_checked() {
        if (value==INVALID_HANDLE_VALUE) return true;
        const auto closing=value;
        value=INVALID_HANDLE_VALUE;
        return io.close(closing)!=0;
    }
};
[[noreturn]] void failure(const FovConfigIo &io,const char *what) {
    throw std::runtime_error(std::string(what)+" (Windows error "+
                             std::to_string(io.error())+")");
}
struct Snapshot {
    bool exists=false;
    std::string bytes;
    BY_HANDLE_FILE_INFORMATION identity{};
};
Snapshot read_current(const FovConfigIo &io,const std::filesystem::path &path) {
    Handle file(io,io.open(path.c_str(),GENERIC_READ,
        FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if (file.value==INVALID_HANDLE_VALUE) {
        if (io.error()==ERROR_FILE_NOT_FOUND) return {};
        failure(io,"Cannot read current config");
    }
    Snapshot out; out.exists=true;
    if (!io.information(file.value,&out.identity)) failure(io,"Cannot inspect config");
    if (out.identity.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Config must be an ordinary file");
    // A bounded host config must not allocate unbounded memory on Save.
    if (out.identity.nFileSizeHigh || out.identity.nFileSizeLow>1024*1024)
        throw std::runtime_error("Config exceeds the 1 MiB host settings limit");
    out.bytes.resize(out.identity.nFileSizeLow);
    DWORD read=0;
    if (!out.bytes.empty() && (!io.read(file.value,out.bytes.data(),
        static_cast<DWORD>(out.bytes.size()),&read,nullptr) || read!=out.bytes.size()))
        failure(io,"Cannot read complete config");
    if (!file.close_checked()) failure(io,"Cannot close current config");
    return out;
}
bool same(const Snapshot &a,const Snapshot &b) {
    return a.exists==b.exists && a.bytes==b.bytes && (!a.exists ||
        (a.identity.dwVolumeSerialNumber==b.identity.dwVolumeSerialNumber &&
         a.identity.nFileIndexHigh==b.identity.nFileIndexHigh &&
         a.identity.nFileIndexLow==b.identity.nFileIndexLow &&
         CompareFileTime(&a.identity.ftLastWriteTime,&b.identity.ftLastWriteTime)==0));
}
} // namespace
std::string merge_fov_config(std::string_view original,const FovOptions &options) {
    validate_fov(options.world,120); validate_fov(options.weapon,104);
    const std::array<std::string,2> values{format(options.world),format(options.weapon)};
    std::array<bool,2> seen{};
    std::string out, newline;
    for (size_t begin=0;begin<original.size();) {
        auto end=original.find('\n',begin);
        bool terminated=end!=std::string_view::npos;
        if (!terminated) end=original.size();
        auto content=original.substr(begin,end-begin);
        auto eol=terminated ? std::string_view("\n") : std::string_view{};
        if (terminated && !content.empty() && content.back()=='\r') {
            content.remove_suffix(1); eol="\r\n";
        }
        if (newline.empty() && !eol.empty()) newline=eol;
        auto line=trim(content);
        bool edited=false;
        if (!line.empty() && line.front()!='#') {
            auto equal=line.find('=');
            if (equal==std::string_view::npos)
                throw std::runtime_error("Current config contains a line without '='");
            auto key=trim(line.substr(0,equal)), value=trim(line.substr(equal+1));
            validate_line(key,value);
            if (key=="world_fov" || key=="weapon_fov") {
                size_t index=key=="world_fov" ? 0u : 1u;
                // Keep original indentation, key/equals spacing, trailing whitespace.
                auto offset=static_cast<size_t>(value.data()-content.data());
                out.append(content.substr(0,offset)); out+=values[index];
                out.append(content.substr(offset+value.size()));
                seen[index]=true; edited=true;
            }
        }
        if (!edited) out.append(content);
        out.append(eol);
        begin=terminated ? end+1 : end;
    }
    if (newline.empty()) newline="\r\n";
    for (size_t i=0;i<seen.size();++i) if (!seen[i]) {
        if (!out.empty() && out.back()!='\n') out+=newline;
        out+=i==0 ? "world_fov = " : "weapon_fov = ";
        out+=values[i]; out+=newline;
    }
    return out;
}
namespace {
FovSaveResult save_config(const std::filesystem::path &path,uint64_t revision,
                         const std::function<std::string(std::string_view)> &merge,
                         const FovConfigIo *injected) {
    const FovConfigIo defaults;
    const auto &io=injected ? *injected : defaults;
    FovSaveResult out{false,revision,{}};
    std::filesystem::path temporary;
    bool owns_temporary=false;
    try {
        const auto before=read_current(io,path);
        const auto merged=merge(before.bytes);
        // The name is generated in the same directory, and CREATE_NEW owns it.
        static std::atomic<uint64_t> serial{0};
        temporary=path; temporary+=L".tmp-"+std::to_wstring(GetCurrentProcessId())+
            L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++serial);
        {
            Handle file(io,io.open(temporary.c_str(),GENERIC_WRITE,0,nullptr,
                CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
            if (file.value==INVALID_HANDLE_VALUE) failure(io,"Cannot create settings temporary file");
            owns_temporary=true; // A colliding name belongs to another writer.
            DWORD written=0;
            if (!io.write(file.value,merged.data(),static_cast<DWORD>(merged.size()),
                &written,nullptr) || written!=merged.size()) failure(io,"Cannot write settings");
            if (!io.flush(file.value)) failure(io,"Cannot flush settings");
            if (!file.close_checked()) failure(io,"Cannot close settings");
        }
        if (!same(before,read_current(io,path)))
            throw std::runtime_error("Config changed during Save; retry after reviewing it");
        // No unlink: replacement failure preserves the destination. For an absent
        // target MoveFileEx without REPLACE_EXISTING refuses a concurrent creation.
        if (before.exists) {
            if (!io.replace(path.c_str(),temporary.c_str(),nullptr,0,nullptr,nullptr))
                failure(io,"Cannot replace settings atomically");
        } else if (!io.move(temporary.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH)) {
            failure(io,"Cannot install settings atomically");
        }
        temporary.clear(); owns_temporary=false; out.saved=true;
    } catch (const std::exception &error) { out.error=error.what(); }
    if (owns_temporary && !io.remove(temporary.c_str())) {
        const auto cleanup_error=io.error();
        if (cleanup_error!=ERROR_FILE_NOT_FOUND)
            out.error+="; cannot clean owned temporary file (Windows error "+
                       std::to_string(cleanup_error)+")";
    }
    return out;
}
} // namespace
FovSaveResult save_fov_config(const std::filesystem::path &path,const FovOptions &visible,
                             uint64_t revision,const FovConfigIo *injected) {
    return save_config(path,revision,[&](auto bytes) { return merge_fov_config(bytes,visible); },injected);
}
std::string merge_display_config(std::string_view original,const DisplaySettings &settings) {
    const std::array<std::string_view,3> keys{"fullscreen","fullscreen_resolution","window_size"};
    const std::array<std::string,3> values{settings.fullscreen ? "true" : "false",
        display_resolution(settings.fullscreen_resolution),display_resolution(settings.window_size)};
    if(!parse_display_resolution(values[1]) || !parse_display_resolution(values[2]))
        throw std::invalid_argument("Invalid output resolution");
    std::array<bool,3> seen{};
    std::string out,newline;
    for(size_t begin=0;begin<original.size();) {
        auto end=original.find('\n',begin); const bool terminated=end!=std::string_view::npos;
        if(!terminated) end=original.size();
        auto content=original.substr(begin,end-begin);
        auto eol=terminated ? std::string_view("\n") : std::string_view{};
        if(terminated && !content.empty() && content.back()=='\r') { content.remove_suffix(1); eol="\r\n"; }
        if(newline.empty() && !eol.empty()) newline=eol;
        const auto line=trim(content); bool edited=false;
        if(!line.empty() && line.front()!='#') {
            const auto eq=line.find('=');
            if(eq==std::string_view::npos) throw std::runtime_error("Current config contains a line without '='");
            const auto key=trim(line.substr(0,eq)),value=trim(line.substr(eq+1));
            validate_line(key,value);
            for(size_t i=0;i<keys.size();++i) if(key==keys[i]) {
                const auto offset=static_cast<size_t>(value.data()-content.data());
                out.append(content.substr(0,offset)); out+=values[i]; out.append(content.substr(offset+value.size()));
                seen[i]=edited=true; break;
            }
        }
        if(!edited) out.append(content);
        out.append(eol); begin=terminated ? end+1 : end;
    }
    if(newline.empty()) newline="\r\n";
    for(size_t i=0;i<keys.size();++i) if(!seen[i]) {
        if(!out.empty() && out.back()!='\n') out+=newline;
        out+=keys[i]; out+=" = "; out+=values[i]; out+=newline;
    }
    return out;
}
FovSaveResult save_display_config(const std::filesystem::path &path,const DisplaySettings &settings,
                                 const FovConfigIo *injected) {
    return save_config(path,0,[&](auto bytes) { return merge_display_config(bytes,settings); },injected);
}
std::string merge_render_config(std::string_view original,GuestExtent extent) {
    const std::array<std::string_view,1> keys{"render_resolution"};
    const std::array<std::string,1> values{render_extent_name(extent)};
    std::array<bool,1> seen{};
    std::string out,newline;
    for(size_t begin=0;begin<original.size();) {
        auto end=original.find('\n',begin); const bool terminated=end!=std::string_view::npos;
        if(!terminated) end=original.size();
        auto content=original.substr(begin,end-begin);
        auto eol=terminated ? std::string_view("\n") : std::string_view{};
        if(terminated && !content.empty() && content.back()=='\r') { content.remove_suffix(1); eol="\r\n"; }
        if(newline.empty() && !eol.empty()) newline=eol;
        const auto line=trim(content); bool edited=false;
        if(!line.empty() && line.front()!='#') {
            const auto eq=line.find('=');
            if(eq==std::string_view::npos) throw std::runtime_error("Current config contains a line without '='");
            const auto key=trim(line.substr(0,eq)),value=trim(line.substr(eq+1));
            validate_line(key,value);
            for(size_t i=0;i<keys.size();++i) if(key==keys[i]) {
                const auto offset=static_cast<size_t>(value.data()-content.data());
                out.append(content.substr(0,offset)); out+=values[i]; out.append(content.substr(offset+value.size()));
                seen[i]=edited=true; break;
            }
        }
        if(!edited) out.append(content);
        out.append(eol); begin=terminated ? end+1 : end;
    }
    if(newline.empty()) newline="\r\n";
    for(size_t i=0;i<keys.size();++i) if(!seen[i]) {
        if(!out.empty() && out.back()!='\n') out+=newline;
        out+=keys[i]; out+=" = "; out+=values[i]; out+=newline;
    }
    return out;
}
FovSaveResult save_render_config(const std::filesystem::path &path,GuestExtent extent,
                                const FovConfigIo *injected) {
    return save_config(path,0,[&](auto bytes) { return merge_render_config(bytes,extent); },injected);
}

std::string merge_weapon_position_config(std::string_view original,weapon_placement::Position position) {
    if(!weapon_placement::valid(position)) throw std::invalid_argument("Invalid weapon position pair");
    position=weapon_placement::canonical(position);
    const std::array<std::string_view,2> keys{"weapon_position_x","weapon_position_y"};
    const std::array<std::string,2> values{format(position.x),format(position.y)};
    std::array<bool,2> seen{};
    std::string out,newline;
    for(size_t begin=0;begin<original.size();) {
        auto end=original.find('\n',begin); const bool terminated=end!=std::string_view::npos;
        if(!terminated) end=original.size();
        auto content=original.substr(begin,end-begin);
        auto eol=terminated ? std::string_view("\n") : std::string_view{};
        if(terminated && !content.empty() && content.back()=='\r') { content.remove_suffix(1); eol="\r\n"; }
        if(newline.empty() && !eol.empty()) newline=eol;
        const auto line=trim(content); bool edited=false;
        if(!line.empty() && line.front()!='#') {
            const auto eq=line.find('=');
            if(eq==std::string_view::npos) throw std::runtime_error("Current config contains a line without '='");
            const auto key=trim(line.substr(0,eq)),value=trim(line.substr(eq+1));
            validate_line(key,value);
            for(size_t i=0;i<keys.size();++i) if(key==keys[i]) {
                const auto offset=static_cast<size_t>(value.data()-content.data());
                out.append(content.substr(0,offset)); out+=values[i]; out.append(content.substr(offset+value.size()));
                seen[i]=edited=true; break;
            }
        }
        if(!edited) out.append(content);
        out.append(eol); begin=terminated ? end+1 : end;
    }
    if(newline.empty()) newline="\r\n";
    for(size_t i=0;i<keys.size();++i) if(!seen[i]) {
        if(!out.empty() && out.back()!='\n') out+=newline;
        out+=keys[i]; out+=" = "; out+=values[i]; out+=newline;
    }
    return out;
}
FovSaveResult save_weapon_position_config(const std::filesystem::path &path,weapon_placement::Position position,
                                         const FovConfigIo *injected) {
    return save_config(path,0,[&](auto bytes) { return merge_weapon_position_config(bytes,position); },injected);
}

static std::string merge_percentage(std::string_view original,std::string_view key,int percent) {
    const std::array<std::string_view,1> keys{key};
    const std::array<std::string,1> values{std::to_string(percent)};
    std::array<bool,1> seen{};
    std::string out,newline;
    for(size_t begin=0;begin<original.size();) {
        auto end=original.find('\n',begin); const bool terminated=end!=std::string_view::npos;
        if(!terminated) end=original.size();
        auto content=original.substr(begin,end-begin);
        auto eol=terminated ? std::string_view("\n") : std::string_view{};
        if(terminated && !content.empty() && content.back()=='\r') { content.remove_suffix(1); eol="\r\n"; }
        if(newline.empty() && !eol.empty()) newline=eol;
        const auto line=trim(content); bool edited=false;
        if(!line.empty() && line.front()!='#') {
            const auto eq=line.find('=');
            if(eq==std::string_view::npos) throw std::runtime_error("Current config contains a line without '='");
            const auto key=trim(line.substr(0,eq)),value=trim(line.substr(eq+1));
            validate_line(key,value);
            for(size_t i=0;i<keys.size();++i) if(key==keys[i]) {
                const auto offset=static_cast<size_t>(value.data()-content.data());
                out.append(content.substr(0,offset)); out+=values[i]; out.append(content.substr(offset+value.size()));
                seen[i]=edited=true; break;
            }
        }
        if(!edited) out.append(content);
        out.append(eol); begin=terminated ? end+1 : end;
    }
    if(newline.empty()) newline="\r\n";
    for(size_t i=0;i<keys.size();++i) if(!seen[i]) {
        if(!out.empty() && out.back()!='\n') out+=newline;
        out+=keys[i]; out+=" = "; out+=values[i]; out+=newline;
    }
    return out;
}
std::string merge_movement_wobble_config(std::string_view original,movement_wobble::Strength strength) {
    return merge_percentage(original,"movement_wobble",strength.percent());
}
std::string merge_ui_scale_config(std::string_view original,int percent) {
    if(!ui::valid_percent(percent)) throw std::invalid_argument("Invalid UI scale");
    return merge_percentage(original,"ui_scale",percent);
}
std::string merge_graphics_quality_config(std::string_view original,GraphicsQuality quality) {
    if(!valid_graphics_quality(quality)) throw std::invalid_argument("Invalid graphics quality");
    return merge_percentage(merge_percentage(original,"lod_distance",quality.lod_distance),
                            "texture_filtering",quality.texture_filtering);
}
FovSaveResult save_graphics_quality_config(const std::filesystem::path &path,GraphicsQuality quality,const FovConfigIo* io) {
    return save_config(path,0,[&](auto bytes) { return merge_graphics_quality_config(bytes,quality); },io);
}
FovSaveResult save_ui_scale_config(const std::filesystem::path &path,int percent,const FovConfigIo* io) {
    return save_config(path,0,[&](auto bytes) { return merge_ui_scale_config(bytes,percent); },io);
}
FovSaveResult save_movement_wobble_config(const std::filesystem::path &path,movement_wobble::Strength strength,
                                const FovConfigIo *injected) {
    return save_config(path,0,[&](auto bytes) { return merge_movement_wobble_config(bytes,strength); },injected);
}

std::string merge_controls_config(std::string_view original,const input::ControlsConfig &bindings) {
    // Validate the existing control file with its actual parser before replacing
    // any bytes. Unlike the general host file this accepts legacy inline comments.
    std::istringstream current{std::string(original)};
    (void)input::ControlsConfig::parse(current);
    std::string out,newline;
    for(size_t begin=0;begin<original.size();) {
        auto end=original.find('\n',begin);
        const bool terminated=end!=std::string_view::npos;
        if(!terminated) end=original.size();
        auto content=original.substr(begin,end-begin);
        auto eol=terminated ? std::string_view("\n") : std::string_view{};
        if(terminated && !content.empty() && content.back()=='\r') {
            content.remove_suffix(1); eol="\r\n";
        }
        if(newline.empty() && !eol.empty()) newline=eol;
        auto line=trim(content.substr(0,content.find('#')));
        const auto key_end=line.find_first_of("= \t");
        const auto key=line.substr(0,key_end);
        if(key!="bindings" && !key.starts_with("bind.")) {
            out.append(content); out.append(eol);
        } else {
            // Preserve a user's inline comment even when its old binding is removed.
            const auto comment=content.find('#');
            if(comment!=std::string_view::npos) { out.append(content.substr(comment)); out.append(eol); }
        }
        begin=terminated ? end+1 : end;
    }
    if(newline.empty()) newline="\r\n";
    if(!out.empty() && out.back()!='\n') out+=newline;
    const auto serialized=input::serialize_bindings(bindings);
    for(char c:serialized) if(c=='\n') out+=newline; else out+=c;
    return out;
}
FovSaveResult save_controls_config(const std::filesystem::path &path,const input::ControlsConfig &bindings,
                                  const FovConfigIo *injected) {
    return save_config(path,0,[&](auto bytes) { return merge_controls_config(bytes,bindings); },injected);
}
std::string merge_mouse_sensitivity(std::string_view original,input::MouseSensitivity values) {
    if(!input::valid_sensitivity(values)) throw std::invalid_argument("Invalid mouse sensitivity");
    std::istringstream current{std::string(original)};
    (void)input::ControlsConfig::parse(current);
    std::string out,newline;
    for(size_t begin=0;begin<original.size();) {
        auto end=original.find('\n',begin);
        const bool terminated=end!=std::string_view::npos;
        if(!terminated) end=original.size();
        auto content=original.substr(begin,end-begin);
        auto eol=terminated ? std::string_view("\n") : std::string_view{};
        if(terminated && !content.empty() && content.back()=='\r') {
            content.remove_suffix(1); eol="\r\n";
        }
        if(newline.empty() && !eol.empty()) newline=eol;
        const auto line=trim(content.substr(0,content.find('#')));
        const auto key=line.substr(0,line.find_first_of("= \t"));
        if(key!="sensitivity" && key!="ads_sensitivity" && key!="aim_multiplier") { out.append(content); out.append(eol); }
        else {
            const auto comment=content.find('#');
            if(comment!=std::string_view::npos) { out.append(content.substr(comment)); out.append(eol); }
        }
        begin=terminated ? end+1 : end;
    }
    if(newline.empty()) newline="\r\n";
    if(!out.empty() && out.back()!='\n') out+=newline;
    out+="sensitivity = "+format(values.normal)+newline;
    out+="aim_multiplier = "+format(values.aim_multiplier)+newline;
    return out;
}
FovSaveResult save_controls_preferences(const std::filesystem::path &path,const input::ControlsConfig &config,
    bool bindings,bool sensitivity,const FovConfigIo *injected) {
    if(!bindings && !sensitivity) return {};
    return save_config(path,0,[&](auto bytes) {
        auto result=bindings ? merge_controls_config(bytes,config) : std::string(bytes);
        return sensitivity ? merge_mouse_sensitivity(result,{config.sensitivity,config.aim_multiplier}) : result;
    },injected);
}
} // namespace nova::platform
