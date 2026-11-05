#include "launcher/shortcut.h"
#include <windows.h>
#include <shobjidl.h>
#include <gdiplus.h>
#include <vector>
#include <memory>
namespace nova::launcher {
namespace {
void check(HRESULT result) { if(FAILED(result)) throw std::runtime_error("Cannot create the game shortcut."); }
template<class T> struct Com {
    T *p=nullptr; ~Com(){if(p) p->Release();}
};
void put(std::vector<unsigned char> &bytes,size_t offset,uint32_t value,size_t count) {
    for(size_t i=0;i<count;++i) bytes[offset+i]=static_cast<unsigned char>(value>>(8*i));
}
}
void create_game_shortcut(const Layout &layout,const fs::path &launcher) {
    const auto link=launcher.parent_path()/L"N.O.V.A. 3.lnk";
    const auto png=layout.root/"local/launcher/icon.png",ico=layout.root/"local/launcher/game.ico";
    if(fs::exists(link) || !fs::is_regular_file(png)) return;
    auto current=layout.root;
    for(const auto &part:fs::path("local/launcher/game.ico")) {
        current/=part; const auto flags=GetFileAttributesW(current.c_str());
        if(flags!=INVALID_FILE_ATTRIBUTES && (flags&FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Shortcut artwork cannot use links or junctions.");
    }
    if(!fs::exists(ico)) {
        ULONG_PTR token=0; Gdiplus::GdiplusStartupInput input;
        if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok) throw std::runtime_error("Cannot initialize icon conversion.");
        struct Stop {ULONG_PTR t; ~Stop(){Gdiplus::GdiplusShutdown(t);}} stop{token};
        Gdiplus::Bitmap source(png.c_str());
        if(source.GetLastStatus()!=Gdiplus::Ok) throw std::runtime_error("Cannot decode the local game icon.");
        Gdiplus::Bitmap scaled(256,256,PixelFormat32bppARGB);
        {
            Gdiplus::Graphics graphics(&scaled);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            graphics.DrawImage(&source,0,0,256,256);
        }
        Com<IStream> stream; check(CreateStreamOnHGlobal(nullptr,TRUE,&stream.p));
        const CLSID png_encoder={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
        if(scaled.Save(stream.p,&png_encoder,nullptr)!=Gdiplus::Ok) throw std::runtime_error("Cannot encode the game icon.");
        STATSTG stat{}; check(stream.p->Stat(&stat,STATFLAG_NONAME));
        if(stat.cbSize.QuadPart>8*1024*1024) throw std::runtime_error("Icon encoding is unexpectedly large.");
        const auto size=static_cast<uint32_t>(stat.cbSize.QuadPart);
        std::vector<unsigned char> bytes(22+size); put(bytes,2,1,2); put(bytes,4,1,2);
        put(bytes,10,1,2); put(bytes,12,32,2); put(bytes,14,size,4); put(bytes,18,22,4);
        LARGE_INTEGER zero{}; check(stream.p->Seek(zero,STREAM_SEEK_SET,nullptr));
        ULONG read=0; check(stream.p->Read(bytes.data()+22,size,&read));
        if(read!=size) throw std::runtime_error("Incomplete icon encoding.");
        auto temporary=ico; temporary+=L".part-"+std::to_wstring(GetCurrentProcessId());
        HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot write the game icon.");
        DWORD written=0; const auto good=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) && written==bytes.size() && FlushFileBuffers(file);
        CloseHandle(file);
        if(!good || !MoveFileExW(temporary.c_str(),ico.c_str(),MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temporary.c_str()); throw std::runtime_error("Cannot save the game icon.");
        }
    }
    const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(initialized) && initialized!=RPC_E_CHANGED_MODE) check(initialized);
    struct Uninit {bool active; ~Uninit(){if(active) CoUninitialize();}} uninit{SUCCEEDED(initialized)};
    Com<IShellLinkW> shell;
    check(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_IShellLinkW,reinterpret_cast<void **>(&shell.p)));
    check(shell.p->SetPath(launcher.c_str())); check(shell.p->SetWorkingDirectory(layout.root.c_str()));
    check(shell.p->SetIconLocation(ico.c_str(),0)); check(shell.p->SetDescription(L"N.O.V.A. 3 PC"));
    Com<IPersistFile> persistence; check(shell.p->QueryInterface(IID_IPersistFile,reinterpret_cast<void **>(&persistence.p)));
    check(persistence.p->Save(link.c_str(),TRUE));
}
}
