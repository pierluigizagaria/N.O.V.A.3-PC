#pragma once
#include <array>
#include <cmath>
#include <optional>

namespace nova::movement_wobble {
using ViewVector=std::array<float,3>;
using ViewMatrix=std::array<float,16>;
struct CameraView { ViewVector eye,target,up; bool operator==(const CameraView&)const=default; };
inline ViewVector transform_view(const ViewMatrix& m,ViewVector v,bool point) {
    ViewVector out{};
    for(unsigned r=0;r<3;++r)out[r]=m[r]*v[0]+m[4+r]*v[1]+m[8+r]*v[2]+(point?m[12+r]:0);
    return out;
}
inline bool finite_view(const CameraView& v) {
    for(const auto& a:{v.eye,v.target,v.up})for(float x:a)if(!std::isfinite(x))return false;
    return true;
}
inline CameraView transform_view(const ViewMatrix& m,const CameraView& v) {
    return {transform_view(m,v.eye,true),transform_view(m,v.target,true),transform_view(m,v.up,false)};
}
inline std::optional<ViewMatrix> inverse_view_basis(const ViewMatrix& m) {
    for(float x:m)if(!std::isfinite(x))return {};
    if(m[3]!=0 || m[7]!=0 || m[11]!=0 || m[15]!=1)return {};
    const double a=m[0],b=m[4],c=m[8],d=m[1],e=m[5],f=m[9],g=m[2],h=m[6],i=m[10];
    const double det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    if(!std::isfinite(det) || std::abs(det)<1e-8)return {};
    ViewMatrix out{float((e*i-f*h)/det),float((f*g-d*i)/det),float((d*h-e*g)/det),0,
        float((c*h-b*i)/det),float((a*i-c*g)/det),float((b*g-a*h)/det),0,
        float((b*f-c*e)/det),float((c*d-a*f)/det),float((a*e-b*d)/det),0,0,0,0,1};
    const auto t=transform_view(out,ViewVector{m[12],m[13],m[14]},false);
    for(unsigned r=0;r<3;++r)out[12+r]=-t[r];
    return out;
}
// Reference is camera-local to its current physical/shake parent. No animation
// sample or scene/model transform is changed. 100 is an exact byte-value bypass.
inline CameraView reduce_camera_view(const CameraView& native,const CameraView& reference,
                                     const ViewMatrix& parent,int percent) {
    if(percent==100)return native;
    auto out=transform_view(parent,reference);
    if(percent==0)return out;
    const float amount=float(percent)/100.f;
    for(unsigned i=0;i<3;++i) {
        out.eye[i]+=(native.eye[i]-out.eye[i])*amount;
        out.target[i]+=(native.target[i]-out.target[i])*amount;
        out.up[i]+=(native.up[i]-out.up[i])*amount;
    }
    return out;
}
}
