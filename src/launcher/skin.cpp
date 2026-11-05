#include "launcher/skin.h"
#include "launcher/pc_mark.h"
#include <objidl.h>
#include <gdiplus.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
namespace nova::launcher {
namespace {
using namespace Gdiplus;
std::string sha256(const std::vector<unsigned char> &bytes) {
    BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) throw std::runtime_error("Font hash unavailable.");
    struct Close {BCRYPT_ALG_HANDLE a; BCRYPT_HASH_HANDLE &h; ~Close(){if(h) BCryptDestroyHash(h); BCryptCloseAlgorithmProvider(a,0);}} close{algorithm,hash};
    std::array<unsigned char,32> digest{};
    if(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0 ||
       BCryptHashData(hash,const_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(bytes.size()),0)<0 ||
       BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)<0) throw std::runtime_error("Font hash failed.");
    std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto b:digest)out<<std::setw(2)<<unsigned(b);return out.str();
}
std::unique_ptr<Bitmap> bitmap(const std::filesystem::path &path) {
    if(!std::filesystem::is_regular_file(path)) return {};
    auto image=std::make_unique<Bitmap>(path.c_str());
    if(image->GetLastStatus()!=Ok) throw std::runtime_error("Invalid launcher artwork: "+path.filename().string());
    return image;
}
std::wstring upper(std::wstring value) {for(auto &c:value)c=static_cast<wchar_t>(towupper(c));return value;}
// Separable box blur of a W x H float image (three passes approximate a Gaussian).
void box_blur(std::vector<float> &v,int w,int h,int radius) {
    std::vector<float> t(v.size());
    for(int pass=0;pass<3;++pass) {
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
            float sum=0; int n=0;
            for(int k=-radius;k<=radius;++k) if(x+k>=0&&x+k<w) {sum+=v[y*w+x+k];++n;}
            t[y*w+x]=sum/n;
        }
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
            float sum=0; int n=0;
            for(int k=-radius;k<=radius;++k) if(y+k>=0&&y+k<h) {sum+=t[(y+k)*w+x];++n;}
            v[y*w+x]=sum/n;
        }
    }
}
// Owner-drawn "PC" mark (pc_mark.h), smoothed into straight 45-degree edges and
// finished like the logo lettering, flat: gradient face and dark teal rim. One cell of padding holds the rim.
constexpr int mark_cell=8;
std::unique_ptr<Bitmap> make_pc_mark(int &ink_height) {
    std::vector<std::string> rows; std::string row;
    for(const char *c=pc_mark;;++c) {
        if(*c=='\n'||!*c) {if(!row.empty())rows.push_back(row);row.clear();if(!*c)break;}
        else row+=*c;
    }
    if(rows.empty())return {};
    const int cols=static_cast<int>(rows[0].size()),lines=static_cast<int>(rows.size());
    const int w=(cols+2)*mark_cell,h=(lines+2)*mark_cell;
    std::vector<float> ink(static_cast<size_t>(w)*h,0.f);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        const int cx=x/mark_cell-1,cy=y/mark_cell-1;
        if(cx>=0&&cy>=0&&cx<cols&&cy<lines&&cx<static_cast<int>(rows[cy].size())&&rows[cy][cx]=='#')ink[y*w+x]=1.f;
    }
    box_blur(ink,w,h,mark_cell/2);
    auto bitmap=std::make_unique<Bitmap>(w,h,PixelFormat32bppARGB);
    BitmapData data{}; Gdiplus::Rect all(0,0,w,h);
    if(bitmap->LockBits(&all,ImageLockModeWrite,PixelFormat32bppARGB,&data)!=Ok)return {};
    for(int y=0;y<h;++y) {
        auto *out=reinterpret_cast<UINT32 *>(static_cast<BYTE *>(data.Scan0)+y*data.Stride);
        for(int x=0;x<w;++x) {
            const float v=ink[y*w+x];
            // Steep ramps around the 0.5 contour keep edges crisp; a lower contour forms the rim.
            const float face=std::clamp((v-.5f)*6.f+.5f,0.f,1.f),rim=std::clamp((v-.1f)*8.f+.5f,0.f,1.f);
            // Logo finish: white-to-pale-blue face over a dark teal rim (same values as make_banner.py).
            const float t=std::clamp(float(y-mark_cell)/float(lines*mark_cell),0.f,1.f);
            const float fr=250+(184-250)*t,fg=253+(228-253)*t,fb=253+(236-253)*t;
            const float r=10+(fr-10)*face,g=38+(fg-38)*face,b=46+(fb-46)*face;
            const auto a=static_cast<UINT32>(rim*255.f+.5f);
            out[x]=(a<<24)|(static_cast<UINT32>(r*rim+.5f)<<16)|(static_cast<UINT32>(g*rim+.5f)<<8)|static_cast<UINT32>(b*rim+.5f);
        }
    }
    bitmap->UnlockBits(&data);
    ink_height=lines*mark_cell;
    return bitmap;
}
// Rows of the N.O.V.A lettering in the logo, as fractions of its height: the
// longest run of well-covered rows in the left half (the "3" and brackets excluded).
std::pair<float,float> letter_band(Bitmap &logo) {
    const int w=static_cast<int>(logo.GetWidth())/2,h=static_cast<int>(logo.GetHeight());
    std::pair<float,float> fallback{.25f,.70f};
    if(w<=0||h<=0)return fallback;
    BitmapData data{}; Gdiplus::Rect half(0,0,w,h);
    if(logo.LockBits(&half,ImageLockModeRead,PixelFormat32bppARGB,&data)!=Ok)return fallback;
    int best_start=0,best_length=0,start=-1;
    for(int y=0;y<=h;++y) {
        int covered=0;
        if(y<h) {
            const auto *in=reinterpret_cast<const UINT32 *>(static_cast<const BYTE *>(data.Scan0)+y*data.Stride);
            for(int x=0;x<w;++x) covered+=(in[x]>>24)>128;
        }
        if(y<h && covered>w*3/10) {if(start<0)start=y;}
        else if(start>=0) {if(y-start>best_length){best_length=y-start;best_start=start;}start=-1;}
    }
    logo.UnlockBits(&data);
    if(!best_length)return fallback;
    return {float(best_start)/h,float(best_start+best_length)/h};
}
// The original artwork carries the identity; the interface sits directly on it
// with a single cyan accent and a warm error tone.
const Color fallback_base(255,10,13,16),    card(110,4,6,8),card_edge(52,255,255,255),dim(255,150,160,166),faint(255,104,114,120),
    accent(255,94,196,214),accent_hover(255,128,214,228),accent_pressed(255,70,160,176),
    ink(255,6,16,20),danger(255,232,118,98),text_main(255,238,242,244);
enum class Step {pending,current,done,failed};
// Logical layout on the 960x540 canvas.
constexpr REAL content_x=48,content_w=400;
SkinControls controls_for(const SkinState &s) {
    SkinControls c{};
    // Compact language picker in the bottom-right corner, always available when idle.
    if(!s.processing)
        for(int i=0;i<language_count;++i)c.language[i]={canvas_width-24-(language_count-i)*30,500,28,22};
    if(s.processing) return c;
    if(s.error) {
        c.retry={48,410,140,38};
        c.select={200,410,200,38};
    } else {
        c.select={148,388,200,38};
    }
    return c;
}
}
SkinControls skin_controls(const SkinState &state) {return controls_for(state);}
struct Skin::Impl {
    ULONG_PTR token=0;
    // Keep verified font bytes and the private collection alive through all drawing.
    // Bind the family directly to this collection; avoid enumerated family clones.
    std::vector<unsigned char> font_bytes;
    std::unique_ptr<PrivateFontCollection> collection;
    std::unique_ptr<FontFamily> family,body_family,strong_family;
    std::unique_ptr<Bitmap> background,logo,app_icon,mark;
    int mark_ink=0; std::pair<float,float> band{.25f,.70f};
    HICON window_icon=nullptr;
    bool authentic=false;
    std::wstring family_name=L"Segoe UI";
    std::string font_hash;
    Impl(){GdiplusStartupInput input;if(GdiplusStartup(&token,&input,nullptr)!=Ok)throw std::runtime_error("Cannot initialize launcher graphics.");}
    ~Impl(){if(window_icon)DestroyIcon(window_icon);background.reset();mark.reset();logo.reset();app_icon.reset();family.reset();body_family.reset();strong_family.reset();collection.reset();GdiplusShutdown(token);}
    enum class Face {display,body,strong};
    struct Style {
        Face face=Face::body; REAL size=14; Color color=text_main;
        StringAlignment align=StringAlignmentNear; bool wrap=false,path=false;
    };
    const FontFamily *family_for(Face face) const {
        return face==Face::display?family.get():face==Face::strong?strong_family.get():body_family.get();
    }
    void text(Graphics &g,const std::wstring &s,RectF r,const Style &style) const {
        if(s.empty())return;
        StringFormat format(StringFormat::GenericTypographic());
        format.SetFormatFlags(style.wrap?0:StringFormatFlagsNoWrap);
        format.SetTrimming(style.path?StringTrimmingEllipsisPath:style.wrap?StringTrimmingWord:StringTrimmingEllipsisCharacter);
        format.SetAlignment(style.align);
        format.SetLineAlignment(style.wrap?StringAlignmentNear:StringAlignmentCenter);
        const auto face=family_for(style.face);
        Font font(face,style.size,FontStyleRegular,UnitPixel);
        if(font.GetLastStatus()!=Ok)throw std::runtime_error("The selected launcher font cannot render.");
        REAL size=style.size;
        if(!style.wrap && !style.path) {
            // Long translations shrink slightly before they would be truncated.
            RectF measured;
            g.MeasureString(s.c_str(),static_cast<INT>(s.size()),&font,PointF(0,0),&format,&measured);
            if(measured.Width>r.Width)size=std::max(size*.78f,size*r.Width/measured.Width);
        }
        Font fitted(face,size,FontStyleRegular,UnitPixel);
        SolidBrush brush(style.color);
        g.DrawString(s.c_str(),static_cast<INT>(s.size()),&fitted,r,&format,&brush);
    }
    // Source rectangle that fills the 16:9 canvas without distortion.
    static RectF cover(Image &image) {
        const REAL w=REAL(image.GetWidth()),h=REAL(image.GetHeight());
        const REAL target=REAL(canvas_width)/canvas_height;
        // Wider artwork is trimmed from the left so the character stays framed on the right.
        if(w/h>target) {const REAL cw=h*target; return RectF(w-cw,0,cw,h);}
        const REAL ch=w/target; return RectF(0,(h-ch)/2,w,ch);
    }
    void art(Graphics &g,Image &image) const {
        const auto source=cover(image);
        ImageAttributes edges; edges.SetWrapMode(WrapModeTileFlipXY);
        g.DrawImage(&image,RectF(0,0,canvas_width,canvas_height),source.X,source.Y,source.Width,source.Height,UnitPixel,&edges);
    }
    void framed(Graphics &g,RectF r,const Color &fill_color,const Color &edge_color,bool dashed=false) const {
        SolidBrush fill(fill_color); g.FillRectangle(&fill,r);
        Pen edge(edge_color,1); if(dashed)edge.SetDashStyle(DashStyleDash);
        g.DrawRectangle(&edge,r.X+.5f,r.Y+.5f,r.Width-1,r.Height-1);
    }
    void button(Graphics &g,SkinRect r,const std::wstring &label,bool primary,int id,const SkinState &s) const {
        if(r.empty())return;
        const bool hover=s.hover==id,pressed=s.pressed==id,focus=s.focused==id&&s.focus_cues;
        const RectF box(REAL(r.x),REAL(r.y),REAL(r.w),REAL(r.h));
        if(primary) {
            SolidBrush fill(pressed?accent_pressed:hover?accent_hover:accent);
            g.FillRectangle(&fill,box);
        } else {
            framed(g,box,pressed?Color(150,4,6,8):hover?Color(150,40,50,58):Color(130,10,14,18),
                hover?Color(110,255,255,255):Color(70,255,255,255));
        }
        Style style; style.face=Face::strong; style.size=14; style.align=StringAlignmentCenter;
        if(primary)style.color=ink;
        text(g,label,RectF(box.X+10,box.Y,box.Width-20,box.Height),style);
        if(focus) {
            Pen ring(Color(220,accent.GetR(),accent.GetG(),accent.GetB()),1.5f);
            g.DrawRectangle(&ring,box.X-3,box.Y-3,box.Width+6,box.Height+6);
        }
    }
    void language_chip(Graphics &g,SkinRect r,Language language,const SkinState &s) const {
        if(r.empty())return;
        const int id=language_first_id+static_cast<int>(language);
        const bool active=s.language==language,hover=s.hover==id,focus=s.focused==id&&s.focus_cues;
        const RectF box(REAL(r.x),REAL(r.y),REAL(r.w),REAL(r.h));
        Style style; style.face=Face::strong; style.size=11.5f; style.align=StringAlignmentCenter;
        style.color=active?accent:hover?text_main:dim;
        text(g,language_code(language),box,style);
        if(active) {SolidBrush line(accent); g.FillRectangle(&line,box.X+7,box.Y+box.Height-2,box.Width-14,1.5f);}
        if(focus) {Pen ring(Color(200,accent.GetR(),accent.GetG(),accent.GetB()),1.f); g.DrawRectangle(&ring,box.X,box.Y,box.Width,box.Height);}
    }
    void marker(Graphics &g,REAL x,REAL y,int number,Step step) const {
        const REAL d=22;
        if(step==Step::done) {
            SolidBrush fill(accent); g.FillEllipse(&fill,x,y,d,d);
            Pen tick(ink,2.f); tick.SetLineJoin(LineJoinRound); tick.SetStartCap(LineCapRound); tick.SetEndCap(LineCapRound);
            PointF points[]{{x+6.5f,y+11.5f},{x+9.5f,y+14.5f},{x+15.5f,y+8.f}};
            g.DrawLines(&tick,points,3);
            return;
        }
        const Color line=step==Step::current?accent:step==Step::failed?danger:Color(120,255,255,255);
        SolidBrush hole(Color(140,0,0,0)); g.FillEllipse(&hole,x,y,d,d);
        Pen ring(line,1.5f); g.DrawEllipse(&ring,x+.75f,y+.75f,d-1.5f,d-1.5f);
        Style style; style.face=Face::strong; style.size=11; style.align=StringAlignmentCenter;
        style.color=step==Step::pending?dim:line;
        text(g,std::to_wstring(number),RectF(x,y,d,d),style);
    }
    REAL measure(Graphics &g,const std::wstring &s,Face face,REAL size) const {
        StringFormat format(StringFormat::GenericTypographic()); format.SetFormatFlags(StringFormatFlagsNoWrap);
        Font font(family_for(face),size,FontStyleRegular,UnitPixel); RectF out;
        g.MeasureString(s.c_str(),static_cast<INT>(s.size()),&font,PointF(0,0),&format,&out);
        return out.Width;
    }
    void steps(Graphics &g,const SkinState &s,int current) const {
        const Text labels[]{Text::step_apk,Text::step_data,Text::step_prepare};
        constexpr REAL y=164,marker_size=22,label_gap=8,line_pad=10;
        REAL widths[3]{},used=0;
        for(int i=0;i<3;++i) {widths[i]=measure(g,tr(s.language,labels[i]),Face::strong,13)+2;used+=marker_size+label_gap+widths[i];}
        // Connectors take the remaining width; very long translations shrink labels instead.
        REAL gap=(content_w-used)/2,scale=1;
        if(gap<2*line_pad+12) {scale=(content_w-2*(2*line_pad+12)-3*(marker_size+label_gap))/(used-3*(marker_size+label_gap));gap=2*line_pad+12;}
        REAL x=content_x;
        for(int i=0;i<3;++i) {
            const Step step=i<current?Step::done:i>current?Step::pending:s.error?Step::failed:Step::current;
            marker(g,x,y,i+1,step);
            Style label; label.face=Face::strong; label.size=13;
            if(step==Step::pending)label.color=dim;
            const REAL width=widths[i]*scale;
            text(g,tr(s.language,labels[i]),RectF(x+marker_size+label_gap,y,width+4,marker_size),label);
            x+=marker_size+label_gap+width;
            if(i<2) {
                Pen link(i<current?Color(200,accent.GetR(),accent.GetG(),accent.GetB()):Color(60,255,255,255),1.5f);
                g.DrawLine(&link,x+line_pad,y+11,x+gap-line_pad,y+11);
                x+=gap;
            }
        }
        SolidBrush rule(Color(36,255,255,255)); g.FillRectangle(&rule,content_x,y+40,content_w,1.f);
    }
    void drop_zone(Graphics &g,const SkinState &s) const {
        framed(g,RectF(content_x,322,content_w,122),s.drop_hover?Color(150,10,40,46):card,
            s.drop_hover?accent:Color(80,255,255,255),true);
        Style title; title.size=15; title.align=StringAlignmentCenter;
        text(g,tr(s.language,s.want_data?Text::drop_data:Text::drop_apk),RectF(content_x+16,334,content_w-32,24),title);
        Style hint; hint.size=12; hint.color=dim; hint.align=StringAlignmentCenter;
        text(g,tr(s.language,Text::or_word),RectF(content_x+16,360,content_w-32,20),hint);
    }
    void progress(Graphics &g,const SkinState &s) const {
        Style op; op.size=14;
        text(g,s.operation,RectF(content_x,322,content_w-70,22),op);
        Style pct; pct.face=Face::strong; pct.size=14; pct.align=StringAlignmentFar; pct.color=dim;
        text(g,std::to_wstring(std::min(s.percent,100u))+L"%",RectF(content_x+content_w-70,322,70,22),pct);
        SolidBrush track(Color(60,255,255,255)),bar(accent);
        g.FillRectangle(&track,content_x,352.f,content_w,3.f);
        g.FillRectangle(&bar,content_x,352.f,content_w*std::min(s.percent,100u)/100.f,3.f);
        const auto first=s.log.size()>visible_log_rows?s.log.size()-visible_log_rows:0;
        for(size_t i=first;i<s.log.size();++i) {
            const REAL y=REAL(370+static_cast<int>(i-first)*22);
            const bool latest=i+1==s.log.size();
            SolidBrush dot(latest?accent:Color(90,255,255,255)); g.FillEllipse(&dot,content_x+1,y+8.5f,5.f,5.f);
            Style row; row.size=12.5f; row.color=latest?text_main:dim;
            text(g,s.log[i],RectF(content_x+16,y,content_w-16,22),row);
        }
    }
    void failure(Graphics &g,const SkinState &s) const {
        const RectF box(content_x,322,content_w,74);
        framed(g,box,Color(150,40,12,10),Color(70,danger.GetR(),danger.GetG(),danger.GetB()));
        SolidBrush mark(danger); g.FillRectangle(&mark,box.X,box.Y,3.f,box.Height);
        Style message; message.size=13; message.wrap=true; message.color=Color(255,246,222,216);
        text(g,s.log.empty()?std::wstring():s.log.back(),RectF(content_x+20,334,content_w-40,54),message);
    }
    void scene(Graphics &g,const SkinState &s) const {
        g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        if(background) {
            art(g,*background);
            // No box: a soft shade from the left keeps text readable while the
            // artwork stays continuous behind the whole interface.
            LinearGradientBrush shade(PointF(0,0),PointF(560,0),Color(225,3,5,7),Color(0,3,5,7));
            REAL factors[]{0,0,.35f,1},positions[]{0,.48f,.72f,1};
            shade.SetBlend(factors,positions,4);
            g.FillRectangle(&shade,0,0,560,canvas_height);
            LinearGradientBrush floor(PointF(0,380),PointF(0,REAL(canvas_height)),Color(0,0,0,0),Color(150,0,0,0));
            g.FillRectangle(&floor,0.f,380.f,REAL(canvas_width),REAL(canvas_height-380));
        } else {SolidBrush base(fallback_base); g.FillRectangle(&base,0,0,canvas_width,canvas_height);}
        if(logo) {
            // Preserve the original logo's proportions inside a 260x90 box, leaving room for "PC".
            const REAL w=REAL(logo->GetWidth()),h=REAL(logo->GetHeight());
            const REAL scale=std::min(260.f/w,90.f/h);
            const RectF placed(content_x-6,40,w*scale,h*scale);
            g.DrawImage(logo.get(),placed);
            if(mark && mark_ink) {
                // "PC" at the N.O.V.A cap height, on the same baseline, just right of the logo.
                const REAL cap=(band.second-band.first)*placed.Height,k=cap/mark_ink,pad=mark_cell*k;
                g.DrawImage(mark.get(),RectF(placed.GetRight()+cap*.12f-pad,placed.Y+band.first*placed.Height-pad,
                    REAL(mark->GetWidth())*k,REAL(mark->GetHeight())*k));
            }
        } else {
            Style style; style.face=Face::display; style.size=32;
            text(g,L"N.O.V.A. 3",RectF(content_x,66,content_w,48),style);
        }
        const int current=!s.apk_ready&&!s.want_data?0:!s.data_ready?1:2;
        steps(g,s,current);
        wchar_t eyebrow[64]{}; swprintf_s(eyebrow,tr(s.language,Text::step_of),unsigned(current+1));
        Style tag; tag.face=Face::strong; tag.size=11; tag.color=s.error?danger:accent;
        text(g,upper(eyebrow),RectF(content_x,218,content_w,16),tag);
        Style heading; heading.face=Face::display; heading.size=21;
        text(g,upper(s.heading),RectF(content_x,236,content_w,32),heading);
        Style body; body.size=14; body.color=Color(255,196,204,208); body.wrap=true;
        text(g,s.description,RectF(content_x,274,content_w,40),body);
        const auto controls=controls_for(s);
        if(s.processing) progress(g,s);
        else if(s.error) failure(g,s);
        else drop_zone(g,s);
        Style privacy; privacy.size=11.5f; privacy.color=faint;
        text(g,tr(s.language,Text::privacy),RectF(content_x,474,content_w,18),privacy);
        button(g,controls.select,s.select_label,!s.error,select_id,s);
        button(g,controls.retry,tr(s.language,Text::retry),true,retry_id,s);
        for(int i=0;i<language_count;++i)language_chip(g,controls.language[i],static_cast<Language>(i),s);
    }
};
Skin::Skin():impl_(std::make_unique<Impl>()){}
Skin::~Skin()=default;
void Skin::load(const std::filesystem::path &root) {
    auto &p=*impl_;
    // Prefer the Windows UI face; fall back to the generic sans family instead of failing.
    auto system_family=[](std::initializer_list<const wchar_t *> names) {
        for(const auto *name:names) {
            auto family=std::make_unique<FontFamily>(name);
            if(family->GetLastStatus()==Ok && family->IsAvailable())return family;
        }
        return std::unique_ptr<FontFamily>(FontFamily::GenericSansSerif()->Clone());
    };
    if(!p.body_family)p.body_family=system_family({L"Segoe UI"});
    if(!p.strong_family)p.strong_family=system_family({L"Segoe UI Semibold",L"Segoe UI"});
    if(!p.authentic) {
        for(const auto &path:{root/"local/launcher/neuropol.ttf",root/"local/input/fonts/neuropol.ttf"}) {
            if(!std::filesystem::is_regular_file(path))continue;
            if(std::filesystem::file_size(path)!=48048)continue;
            std::ifstream file(path,std::ios::binary);p.font_bytes.assign(std::istreambuf_iterator<char>(file),{});
            p.font_hash=sha256(p.font_bytes);
            if(p.font_hash!="da2e59f53afef9135ca6c1d6109126ced1a67a41fb426176be8963982b684ea0")continue;
            p.family.reset();p.collection=std::make_unique<PrivateFontCollection>();
            if(p.collection->AddFontFile(path.c_str())!=Ok || p.collection->GetFamilyCount()!=1)
                throw std::runtime_error("Cannot load the original Neuropol font.");
            p.family=std::make_unique<FontFamily>(L"Neuropol",p.collection.get());
            wchar_t name[LF_FACESIZE]{};p.family->GetFamilyName(name);p.family_name=name;
            Font witness(p.family.get(),31,FontStyleRegular,UnitPixel);
            p.authentic=witness.GetLastStatus()==Ok && std::wstring(name)==L"Neuropol";
            if(!p.authentic)throw std::runtime_error("The actual drawing font is not Neuropol.");
            break;
        }
    }
    if(!p.family)p.family=std::unique_ptr<FontFamily>(p.body_family->Clone());
    if(!p.background)p.background=bitmap(root/"local/launcher/background-a.png");
    if(!p.logo) {
        p.logo=bitmap(root/"local/launcher/logo-original.png");
        if(p.logo)p.band=letter_band(*p.logo);
    }
    if(!p.mark)p.mark=make_pc_mark(p.mark_ink);
    if(!p.app_icon) {p.app_icon=bitmap(root/"local/launcher/icon.png");if(p.app_icon)p.app_icon->GetHICON(&p.window_icon);}
}
bool Skin::original_font()const{return impl_->authentic;}
bool Skin::complete()const{return original_font() && impl_->background && impl_->logo && impl_->app_icon;}
HICON Skin::icon()const{return impl_->window_icon;}
void Skin::draw(HDC dc,int width,int height,const SkinState &state,int ox,int oy) {
    if(width<=0||height<=0)return;
    Graphics graphics(dc);graphics.TranslateTransform(REAL(-ox),REAL(-oy));graphics.ScaleTransform(REAL(width)/canvas_width,REAL(height)/canvas_height);
    impl_->scene(graphics,state);
}
void Skin::preview(const std::filesystem::path &path,const SkinState &state,int width,int height) {
    Bitmap output(width,height,PixelFormat32bppARGB);
    {Graphics graphics(&output);graphics.ScaleTransform(REAL(width)/canvas_width,REAL(height)/canvas_height);impl_->scene(graphics,state);}
    const CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    if(output.Save(path.c_str(),&png,nullptr)!=Ok)throw std::runtime_error("Cannot save launcher render.");
}
std::string Skin::audit()const {
    std::ostringstream s;s<<"renderer=GDI+ private FontFamily object\nfont_family="<<std::string(impl_->family_name.begin(),impl_->family_name.end())
        <<"\nfont_sha256="<<impl_->font_hash<<"\noriginal_font_verified="<<original_font()<<"\nappearance_complete="<<complete()<<"\n";
    return s.str();
}
}
