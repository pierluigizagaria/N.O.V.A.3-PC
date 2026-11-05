#pragma once
#include <charconv>
#include <optional>
#include <string>
#include <string_view>

namespace nova::platform {
// Legacy output preferences, independent of the immutable native render extent.
struct OutputResolution {
    int width=0,height=0; // 0/0: desktop fullscreen, or preserved window placement.
    bool operator==(const OutputResolution &) const = default;
};
struct DisplaySettings {
    bool fullscreen=false;
    OutputResolution fullscreen_resolution{},window_size{};
    bool operator==(const DisplaySettings &) const = default;
};
inline std::optional<OutputResolution> parse_display_resolution(std::string_view text) {
    OutputResolution value;
    if(text=="desktop") return value;
    const auto split=text.find('x');
    if(split==std::string_view::npos) return {};
    auto read=[](std::string_view part,int &out) {
        const auto result=std::from_chars(part.data(),part.data()+part.size(),out);
        return result.ec==std::errc{} && result.ptr==part.data()+part.size();
    };
    if(!read(text.substr(0,split),value.width) || !read(text.substr(split+1),value.height) ||
       value.width<640 || value.height<360 || value.width>16384 || value.height>16384) return {};
    return value;
}
inline std::string display_resolution(const OutputResolution &value) {
    return value.width==0 && value.height==0 ? "desktop" :
        std::to_string(value.width)+"x"+std::to_string(value.height);
}
} // namespace nova::platform
