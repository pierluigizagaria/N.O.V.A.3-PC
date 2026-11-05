#pragma once
#include <array>
#include <cmath>
#include <optional>
namespace nova::ui {
// Provisional calibration: the existing1080p appearance is the pixel baseline.
inline float resolution_pixel_factor(unsigned height) {
    return height ? 1080.f/height : 1.f;
}
using PixelMatrix=std::array<float,16>;
using PixelVector=std::array<float,4>;
inline PixelVector project_vector(const PixelMatrix& m,const PixelVector& p) {
    PixelVector r{};
    for(unsigned row=0;row<4;++row)
        for(unsigned col=0;col<4;++col) r[row]+=m[col*4+row]*p[col];
    return r;
}
// Apply the same2D similarity to every projected vertex, including vertices
// at different depths. Scaling a scene node's XYZ would change perspective.
inline std::optional<PixelMatrix> pixel_projection(const PixelMatrix& projection,
        const PixelMatrix& view,PixelVector pivot,float factor) {
    const auto clip=project_vector(projection,project_vector(view,pivot));
    if(!std::isfinite(factor) || factor<=0 || !std::isfinite(clip[3]) || std::abs(clip[3])<1e-6f) return {};
    const float x=clip[0]/clip[3],y=clip[1]/clip[3];
    if(!std::isfinite(x) || !std::isfinite(y)) return {};
    auto result=projection;
    for(unsigned col=0;col<4;++col) {
        result[col*4]=factor*projection[col*4]+(1-factor)*x*projection[col*4+3];
        result[col*4+1]=factor*projection[col*4+1]+(1-factor)*y*projection[col*4+3];
    }
    return result;
}
}
