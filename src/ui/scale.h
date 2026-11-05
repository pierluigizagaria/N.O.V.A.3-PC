#pragma once
#include <array>
#include <atomic>
#include <charconv>
#include <optional>
#include <string_view>
namespace nova::ui {
constexpr int default_percent=100,minimum_percent=50,maximum_percent=300;
inline bool valid_percent(int value) { return value>=minimum_percent && value<=maximum_percent; }
inline std::optional<int> parse_percent(std::string_view text) {
    int value=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !valid_percent(value)) return {};
    return value;
}
// The slider composes once with the GUI's separate render-pixel policy.
inline float hud_factor(int percent) { return percent/100.f; }
// Menu entry scopes also cover nested level/HUD draws behind the pause page.
inline thread_local unsigned menu_draw_depth=0;
struct MenuDrawScope {
    MenuDrawScope() { ++menu_draw_depth; }
    ~MenuDrawScope() { --menu_draw_depth; }
};
struct Point { float x=0,y=0; };
// Match native GetScaledPosX/Y integer truncation on its480x320 fitted canvas.
inline Point fitted_anchor(unsigned width,unsigned height,Point p) {
    const float sx=width*(1.f/480),sy=height*(1.f/320);
    const float fit=sx<sy ? sx : sy;
    return {float((int(width)-int(480*fit))/2+int(p.x*fit)),
            float((int(height)-int(320*fit))/2+int(p.y*fit))};
}
inline Point transform(Point p,Point anchor,float factor) {
    return {anchor.x+(p.x-anchor.x)*factor,anchor.y+(p.y-anchor.y)*factor};
}
inline void scale_projection(std::array<float,16>& matrix,Point anchor,float factor) {
    matrix[12]+=matrix[0]*anchor.x*(1-factor);
    matrix[13]+=matrix[5]*anchor.y*(1-factor);
    matrix[0]*=factor; matrix[5]*=factor;
}
class RequestedScale {
public:
    explicit RequestedScale(int value=default_percent):value_(valid_percent(value)?value:default_percent) {}
    int percent() const noexcept { return value_.load(std::memory_order_acquire); }
    void set(int value) noexcept { if(valid_percent(value)) value_.store(value,std::memory_order_release); }
private:
    std::atomic<int> value_;
};
// Scoped to a checked native viewport call: native layout remains logical.
inline thread_local unsigned raster_factor=1;
struct RasterScope {
    unsigned previous;
    explicit RasterScope(unsigned factor):previous(raster_factor) { raster_factor=factor; }
    ~RasterScope() { raster_factor=previous; }
};
}
