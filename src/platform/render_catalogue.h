#pragma once
#include "platform/guest_extent.h"
#include <span>
#include <vector>

namespace nova::platform {
struct RenderLimits {
    int texture=0,renderbuffer=0,viewport_width=0,viewport_height=0;
    bool storage(int width,int height) const {
        return width>0 && height>0 && width<=texture && height<=texture &&
            width<=renderbuffer && height<=renderbuffer &&
            width<=viewport_width && height<=viewport_height;
    }
    bool admits(GuestExtent extent) const {
        return supported_render_extent(extent) && storage(extent.width,extent.height);
    }
};
struct RenderChoice {
    GuestExtent extent;
    bool display_mode=false,recommended=false;
};
// Display modes annotate physical matches; they never resize the window or
// introduce an aspect/size without the finite native render contract above.
inline std::vector<RenderChoice> render_catalogue(RenderLimits limits,
        std::span<const GuestExtent> display_modes={},GuestExtent desktop={0,0}) {
    std::vector<RenderChoice> out;
    for(const auto extent:standard_render_extents) {
        if(!limits.admits(extent)) continue;
        const bool mode=extent==desktop || std::find(display_modes.begin(),display_modes.end(),extent)!=display_modes.end();
        out.push_back({extent,mode,false});
    }
    // Prefer the exact desktop size, or the largest supported16:9 extent that
    // fits it. Saved/active selections stay authoritative; this is a UI hint.
    for(auto it=out.rbegin();it!=out.rend();++it)
        if(it->extent.width<=desktop.width && it->extent.height<=desktop.height) {
            it->recommended=true; break;
        }
    return out;
}
} // namespace nova::platform
