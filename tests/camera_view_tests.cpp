#include "movement_wobble/camera_view.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <limits>
using namespace nova::movement_wobble;
namespace {
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
void near(const CameraView& a,const CameraView& b) {
    for(unsigned i=0;i<3;++i)check(std::abs(a.eye[i]-b.eye[i])<.0001f &&
        std::abs(a.target[i]-b.target[i])<.0001f && std::abs(a.up[i]-b.up[i])<.0001f,"view vectors disagree");
}
}
int main() {
    try {
        const CameraView reference{{0,0,0},{0,1,0},{0,0,1}};
        const ViewMatrix identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        for(float phase:{-.05f,0.f,.07f}) {
            const CameraView bob{{phase,0,phase*.5f},{phase,1,phase},{0,phase,1}};
            // Rotate, translate and scale the parent while the walk oscillates.
            // Zero removes only the relative oscillation, never physical input.
            for(const auto& parent:{identity,ViewMatrix{0,2,0,0,-2,0,0,0,0,0,2,0,50,-30,8,1}}) {
                const auto inverse=inverse_view_basis(parent);check(bool(inverse),"valid parent inverse");
                const auto native=transform_view(parent,bob),steady=transform_view(parent,reference);
                near(transform_view(*inverse,native),bob);
                near(reduce_camera_view(native,reference,parent,0),steady);
                const auto full=reduce_camera_view(native,reference,parent,100);
                check(!std::memcmp(&full,&native,sizeof full),"100 preserves exact native input");
                const auto half=reduce_camera_view(native,reference,parent,50);
                check(std::abs(half.eye[0]-(steady.eye[0]+native.eye[0])*.5f)<.0001f,"half amplitude");
                check(std::abs(half.target[2]-(steady.target[2]+native.target[2])*.5f)<.0001f,"half angular target");
            }
        }
        auto invalid=identity;invalid[0]=0;check(!inverse_view_basis(invalid),"singular parent rejected");
        invalid=identity;invalid[3]=1;check(!inverse_view_basis(invalid),"projective parent rejected");
        invalid=identity;invalid[0]=std::numeric_limits<float>::quiet_NaN();check(!inverse_view_basis(invalid),"nonfinite parent rejected");
        std::puts("Camera view: zero/half/native amplitude, mouse/physical parent motion, inverse and invalid inputs passed");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
