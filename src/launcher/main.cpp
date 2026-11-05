#include "launcher/setup.h"
#include "launcher/process.h"
#include "launcher/language.h"
#include "launcher/shortcut.h"
#include "launcher/skin.h"
#include <fstream>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <mutex>
#include <thread>
#include <memory>

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {
using namespace nova::launcher;
constexpr UINT notice=WM_APP+1,finished=WM_APP+2;
constexpr UINT_PTR hover_timer=1;
struct App {
    HWND window=nullptr,log=nullptr,select=nullptr,retry=nullptr,languages[language_count]{};
    int dpi=96,width=canvas_width,height=canvas_height,hover=0,pressed=0;
    bool focus_cues=true,data_ready=false;
    Language language=system_language();
    Skin skin;
    Layout layout; fs::path executable,launcher,apk,data;
    bool busy=false,closing=false,preview=false,preview_progress=false,error=false,success=false,apk_ready=false;
    RequiredInput wanted=RequiredInput::apk;
    unsigned percent=0;
    size_t scroll_back=0;
    std::wstring headline,description,stage;
    std::vector<std::wstring> visible_log;
    std::thread worker; std::atomic_bool cancelled=false;
    std::mutex mutex; std::deque<std::string> messages; unsigned pending_percent=0;
    std::string failure; int outcome=0; HANDLE game=nullptr;
    ~App() {cancelled=true;if(worker.joinable())worker.join();}
    int px(int n) const {return MulDiv(n,dpi,96);}
    const wchar_t *label(Text id) const {return tr(language,id);}
    void appearance() {
        skin.load(layout.root);

    }
    SkinState state() const {
        SkinState result;result.language=language;result.heading=headline;result.description=description;
        const bool other=error;
        result.operation=stage;result.select_label=label(other?Text::other_file:wanted==RequiredInput::apk?Text::select_apk:Text::select_data);
        result.percent=percent;result.processing=busy||preview_progress;result.apk_ready=apk_ready;result.data_ready=data_ready;
        result.want_data=wanted==RequiredInput::data;
        result.apk_name=apk.filename().wstring();result.data_name=data.filename().wstring();
        result.error=error;result.retry=error;result.focused=GetDlgCtrlID(GetFocus());
        result.hover=hover;result.pressed=pressed;result.focus_cues=focus_cues;
        const auto end=visible_log.size()-std::min(scroll_back,visible_log.size());
        result.log.assign(visible_log.begin(),visible_log.begin()+end);return result;
    }
};
std::wstring widen(const std::string &s) {
    if(s.empty())return {};
    const auto n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);
    std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n);return out;
}
void paint(App &a,HDC target) {
    RECT bounds{};GetClientRect(a.window,&bounds);if(!bounds.right||!bounds.bottom)return;
    auto dc=CreateCompatibleDC(target);auto bitmap=CreateCompatibleBitmap(target,bounds.right,bounds.bottom);
    auto previous=SelectObject(dc,bitmap);a.skin.draw(dc,bounds.right,bounds.bottom,a.state());
    BitBlt(target,0,0,bounds.right,bounds.bottom,dc,0,0,SRCCOPY);
    SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);
}
void layout(App &a) {
    RECT r{};GetClientRect(a.window,&r);a.width=r.right;a.height=r.bottom;
    const auto controls=skin_controls(a.state());
    auto move=[&](HWND control,SkinRect box) {
        // Hidden controls keep their tab stop out of the keyboard order.
        ShowWindow(control,box.empty()?SW_HIDE:SW_SHOW);
        if(box.empty())return;
        MoveWindow(control,MulDiv(box.x,a.width,canvas_width),MulDiv(box.y,a.height,canvas_height),
            MulDiv(box.w,a.width,canvas_width),MulDiv(box.h,a.height,canvas_height),TRUE);
    };
    move(a.select,controls.select);
    move(a.retry,controls.retry);
    for(int i=0;i<language_count;++i)move(a.languages[i],controls.language[i]);
    SetWindowTextW(a.select,a.state().select_label.c_str());
    RedrawWindow(a.window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
}
void append_log(App &a,const std::string &message) {
    const auto entry=display_log(a.language,message);
    if(!a.visible_log.empty() && a.visible_log.back()==entry)return;
    a.visible_log.push_back(entry);a.scroll_back=0;
    if(a.visible_log.size()>128)a.visible_log.erase(a.visible_log.begin());
    std::wstring accessible;
    for(const auto &line:a.visible_log)accessible+=line+L"\r\n";
    SetWindowTextW(a.log,accessible.c_str());
}
fs::path language_file(const App &a) {return a.layout.root/"local/launcher/language.txt";}
// The user's explicit choice wins over the Windows UI language on later starts.
void load_language(App &a) {
    std::error_code ec;
    if(!fs::is_regular_file(language_file(a),ec))return;
    std::wifstream file(language_file(a)); std::wstring code; file>>code;
    if(code.size()==2)a.language=language_for_locale(code);
}
void set_language(App &a,Language language) {
    a.language=language;
    a.headline=a.label(a.error?Text::error_title:a.wanted==RequiredInput::apk?Text::welcome:Text::data_title);
    a.description=a.label(a.error?Text::error_help:a.wanted==RequiredInput::apk?Text::apk_help:Text::data_help);
    a.stage=a.label(a.wanted==RequiredInput::apk?Text::waiting_apk:Text::waiting_data);
    if(!a.preview) try {
        fs::create_directories(language_file(a).parent_path());
        std::wofstream(language_file(a))<<language_code(language)<<L"\n";
    } catch(const std::exception &) { /* Preference only; the picker still applies for this run. */ }
    layout(a);
}
void start(App &a) {
    if(a.busy || a.preview) return;
    a.busy=true; a.error=false; a.cancelled=false; a.percent=0; a.apk_ready=false; a.data_ready=false; a.scroll_back=0;
    a.headline=a.label(Text::preparing);
    a.description=a.label(Text::auto_start);
    SetWindowTextW(a.select,a.label(Text::busy)); EnableWindow(a.select,FALSE); ShowWindow(a.retry,SW_HIDE); layout(a);
    InvalidateRect(a.window,nullptr,FALSE);
    a.worker=std::thread([&a] {
        int result=0; std::string failure;
        try {
            unsigned last=101;
            Progress progress{
                [&](const std::string &message) {
                    {std::lock_guard guard(a.mutex); a.messages.push_back(message); a.pending_percent=0;}
                    last=101; PostMessageW(a.window,notice,0,0);
                },
                [&](unsigned percent) {
                    if(percent==last) return; last=percent;
                    {std::lock_guard guard(a.mutex); a.pending_percent=percent;}
                    PostMessageW(a.window,notice,0,0);
                },&a.cancelled};
            prepare(a.layout,a.apk,a.data,progress);
        } catch(const InputRequired &e) { result=e.kind==RequiredInput::apk ? 1 : 2; failure=e.what(); }
        catch(const Cancelled &e) { result=3; failure=e.what(); }
        catch(const std::exception &e) { result=4; failure=e.what(); }
        {std::lock_guard guard(a.mutex); a.outcome=result; a.failure=failure;}
        PostMessageW(a.window,finished,0,0);
    });
}
fs::path pick(HWND owner,RequiredInput kind,Language language) {
    std::wstring buffer(32768,L'\0');
    OPENFILENAMEW dialog{}; dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=owner;
    dialog.lpstrFile=buffer.data(); dialog.nMaxFile=static_cast<DWORD>(buffer.size());
    dialog.lpstrFilter=kind==RequiredInput::apk ? L"Android game package (*.apk)\0*.apk\0\0" : L"Game data (*.zip; *.obb)\0*.zip;*.obb\0\0";
    dialog.lpstrTitle=tr(language,kind==RequiredInput::apk?Text::select_apk:Text::select_data);
    dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER;
    if(!GetOpenFileNameW(&dialog)) return {};
    buffer.resize(wcslen(buffer.c_str())); return fs::path(buffer);
}
void finish(App &a) {
    if(a.worker.joinable()) a.worker.join(); a.busy=false;
    if(a.closing || a.outcome==3) { DestroyWindow(a.window); return; }
    if(a.outcome==0) {
        try {create_game_shortcut(a.layout,a.launcher);} catch(const std::exception &e) {append_log(a,e.what());}
        try { a.game=start_game(a.layout,a.executable); a.success=true; DestroyWindow(a.window); return; }
        catch(const std::exception &e) { a.outcome=4; a.failure=e.what(); }
    }
    a.error=a.outcome==4;
    if(a.outcome==1 || a.outcome==2) a.wanted=a.outcome==1?RequiredInput::apk:RequiredInput::data;
    if(a.error && fs::is_regular_file(a.layout.library()) && a.failure.find("APK")==std::string::npos)
        a.wanted=RequiredInput::data;
    a.headline=a.label(a.error?Text::error_title:a.wanted==RequiredInput::apk?Text::welcome:Text::data_title);
    a.description=a.label(a.error?Text::error_help:a.wanted==RequiredInput::apk?Text::apk_help:Text::data_help);
    a.stage=a.label(a.wanted==RequiredInput::apk?Text::waiting_apk:Text::waiting_data);
    a.apk_ready=a.wanted==RequiredInput::data; a.data_ready=false;
    EnableWindow(a.select,TRUE);
    append_log(a,a.failure); layout(a); InvalidateRect(a.window,nullptr,FALSE); SetFocus(a.select);
}
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    auto *a=reinterpret_cast<App *>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {
        a=static_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams);
        a->window=window; SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(a));
    }
    if(!a) return DefWindowProcW(window,message,wparam,lparam);
    switch(message) {
    case WM_CREATE: {
        auto button=[&](const wchar_t *name,int id) {
            return CreateWindowW(L"BUTTON",name,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
                0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);
        };
        a->select=button(a->label(Text::select_apk),select_id); a->retry=button(a->label(Text::retry),retry_id);
        for(int i=0;i<language_count;++i)
            a->languages[i]=button(language_code(static_cast<Language>(i)),language_first_id+i);
        ShowWindow(a->retry,SW_HIDE);
        DragAcceptFiles(window,TRUE);
        a->log=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_OWNERDRAW,
            0,0,0,0,window,nullptr,nullptr,nullptr);
        a->appearance(); layout(*a); return 0;
    }
    case WM_SIZE: layout(*a); return 0;
    case WM_GETMINMAXINFO: {
        auto info=reinterpret_cast<MINMAXINFO *>(lparam);
        info->ptMinTrackSize={a->px(720),a->px(405)}; return 0;
    }
    case WM_DPICHANGED: {
        a->dpi=HIWORD(wparam); auto r=reinterpret_cast<RECT *>(lparam);
        SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE); return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT p{}; auto dc=BeginPaint(window,&p); paint(*a,dc); EndPaint(window,&p); return 0; }
    case WM_DRAWITEM: {
        auto item=reinterpret_cast<DRAWITEMSTRUCT *>(lparam);POINT origin{};
        a->pressed=(item->itemState&ODS_SELECTED)?static_cast<int>(item->CtlID):0;
        a->focus_cues=!(item->itemState&ODS_NOFOCUSRECT);
        MapWindowPoints(item->hwndItem,window,&origin,1);
        a->skin.draw(item->hDC,a->width,a->height,a->state(),origin.x,origin.y);return TRUE;
    }
    case WM_SETCURSOR: {
        // DefWindowProc of each child asks the parent first, so this sees every button.
        const auto over=reinterpret_cast<HWND>(wparam);
        const int id=over!=window&&IsWindowEnabled(over)?GetDlgCtrlID(over):0;
        if(id!=a->hover) {
            a->hover=id; RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
            if(id)SetTimer(window,hover_timer,80,nullptr);
        }
        if(id){SetCursor(LoadCursorW(nullptr,MAKEINTRESOURCEW(32649)));return TRUE;}
        break;
    }
    case WM_TIMER:
        if(wparam==hover_timer) {
            POINT cursor{}; GetCursorPos(&cursor);
            const auto over=WindowFromPoint(cursor);
            if(!a->hover || !over || GetDlgCtrlID(over)!=a->hover || GetParent(over)!=window) {
                KillTimer(window,hover_timer);
                if(a->hover){a->hover=0;RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);}
            }
            return 0;
        }
        break;
    case WM_DROPFILES: {
        const auto drop=reinterpret_cast<HDROP>(wparam);
        std::wstring name(32768,L'\0');
        const auto length=DragQueryFileW(drop,0,name.data(),static_cast<UINT>(name.size()));
        DragFinish(drop);
        if(a->busy || a->preview || !length) return 0;
        name.resize(length); fs::path dropped(name);
        auto extension=dropped.extension().wstring();
        for(auto &c:extension)c=static_cast<wchar_t>(towlower(c));
        if(extension==L".apk") { a->apk=dropped; a->data.clear(); start(*a); }
        else if(extension==L".zip" || extension==L".obb") { a->data=dropped; start(*a); }
        return 0;
    }
    case WM_MOUSEWHEEL:
        if(static_cast<short>(HIWORD(wparam))>0)a->scroll_back=std::min(a->scroll_back+1,a->visible_log.size()>visible_log_rows?a->visible_log.size()-visible_log_rows:0);
        else if(a->scroll_back)--a->scroll_back;
        InvalidateRect(window,nullptr,FALSE);InvalidateRect(a->log,nullptr,FALSE);return 0;
    case notice: {
        std::deque<std::string> messages;
        {std::lock_guard guard(a->mutex); messages.swap(a->messages); a->percent=a->pending_percent;}
        for(const auto &s:messages) {
            append_log(*a,s);
            if(s.starts_with("Verifying main") || s.starts_with("Importing main") || s.starts_with("Preparing "))a->apk_ready=true;
            if(s.starts_with("Preparing ") || s.starts_with("Ready."))a->data_ready=true;
            a->stage=display_log(a->language,s);
        }
        if(!a->skin.complete())a->appearance();
        InvalidateRect(a->log,nullptr,FALSE);
        InvalidateRect(window,nullptr,FALSE); return 0;
    }
    case finished: finish(*a); return 0;
    case WM_COMMAND:
        // No Cancel button: the caption close button or Escape closes (and safely stops setup).
        if(LOWORD(wparam)==IDCANCEL) { SendMessageW(window,WM_CLOSE,0,0); return 0; }
        if(LOWORD(wparam)==retry_id) { start(*a); return 0; }
        if(LOWORD(wparam)>=language_first_id && LOWORD(wparam)<language_first_id+language_count && !a->busy) {
            set_language(*a,static_cast<Language>(LOWORD(wparam)-language_first_id)); return 0;
        }
        if(LOWORD(wparam)==select_id && !a->busy && !a->preview) {
            auto selected=pick(window,a->wanted,a->language);
            if(!selected.empty()) {
                if(a->wanted==RequiredInput::apk) { a->apk=selected; a->data.clear(); }
                else a->data=selected;
                start(*a);
            }
            return 0;
        }
        break;
    case WM_CLOSE:
        if(a->busy) {
            a->closing=true; a->cancelled=true; a->headline=a->label(Text::stopping);
            a->description=a->label(Text::stopping_help);
            InvalidateRect(window,nullptr,FALSE);
        } else DestroyWindow(window);
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window,message,wparam,lparam);
}
int wait_game(HANDLE game,Language language) {
    WaitForSingleObject(game,INFINITE); DWORD code=1; GetExitCodeProcess(game,&code); CloseHandle(game);
    if(code) MessageBoxW(nullptr,(std::wstring(tr(language,Text::game_failed))+std::to_wstring(code)).c_str(),
        L"N.O.V.A. 3",MB_OK|MB_ICONERROR);
    return code ? 1 : 0;
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command,int) {
    SetProcessDPIAware();
    try {
        const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        struct ComScope {HRESULT result;~ComScope(){if(SUCCEEDED(result))CoUninitialize();}} com_scope{com};
        App app;
        const bool setup=std::wstring(command)==L"--setup";
        app.headline=app.label(Text::preparing); app.description=app.label(Text::checking); app.stage=app.label(Text::checking);
        // UI-only preview: no setup, disk writes, imported data or game process.
        app.preview=std::wstring(command)==L"--preview" || std::wstring(command)==L"--preview-progress";
        std::wstring module(32768,L'\0'); const auto count=GetModuleFileNameW(nullptr,module.data(),static_cast<DWORD>(module.size()));
        if(!count || count>=module.size()) throw std::runtime_error("Cannot locate the launcher.");
        module.resize(count); app.launcher=fs::path(module); const auto directory=app.launcher.parent_path();
        app.layout.root=installation_root(directory);
        load_language(app);
        // Release packages ship NOVA3-Game.exe; development builds keep nova3pc.exe.
        app.executable=directory/L"NOVA3-Game.exe";
        if(!fs::is_regular_file(app.executable) && fs::is_regular_file(directory/L"nova3pc.exe"))app.executable=directory/L"nova3pc.exe";
        if(std::wstring(command)==L"--render-preview" || std::wstring(command)==L"--verify-appearance") {
            // Offline renders of every setup state through the production painter.
            // --verify-appearance additionally requires the original local artwork.
            app.appearance();
            const bool verify=std::wstring(command)==L"--verify-appearance";
            if(verify && !app.skin.complete())return 77;
            const auto output=app.layout.root/"local/logs/l03-launcher";fs::create_directories(output);
            auto reset=[&] {
                app.visible_log.clear();app.preview_progress=false;app.percent=0;app.apk_ready=false;app.data_ready=false;
                app.error=false;app.apk.clear();app.data.clear();app.wanted=RequiredInput::apk;
                app.headline=app.label(Text::welcome);app.description=app.label(Text::apk_help);
            };
            reset();
            app.skin.preview(output/"implemented-first-run.png",app.state());
            reset();app.preview_progress=true;app.apk_ready=true;app.percent=64;app.apk=L"nova3-1.0.7-x86.apk";
            app.data=L"nova3-data.zip";
            app.headline=app.label(Text::preparing);app.description=app.label(Text::auto_start);
            app.stage=localized_log(app.language,"Importing main.1050.com.gameloft.android.ANMP.GloftN3HM.obb");
            app.visible_log={app.label(Text::verify_apk),app.label(Text::library_verified),app.stage};
            app.skin.preview(output/"implemented-progress.png",app.state());
            app.skin.preview(output/"implemented-progress-150.png",app.state(),canvas_width*3/2,canvas_height*3/2);
            reset();app.wanted=RequiredInput::data;app.apk_ready=true;app.apk=L"nova3-1.0.7-x86.apk";
            app.headline=app.label(Text::data_title);app.description=app.label(Text::data_help);
            app.skin.preview(output/"implemented-data.png",app.state());
            reset();app.error=true;app.apk=L"base.apk";
            app.headline=app.label(Text::error_title);app.description=app.label(Text::error_help);
            app.visible_log={L"This APK is not the supported N.O.V.A. 3 1.0.7 x86 build. Select the compatible APK."};
            app.skin.preview(output/"implemented-error.png",app.state());
            std::ofstream(output/"appearance-verification.txt")<<app.skin.audit();return 0;
        }
        // One setup writer/game launch per installation, including the hidden wait.
        auto mutex_name=L"Local\\Nova3Launcher-"+std::to_wstring(std::hash<std::wstring>{}(app.layout.root.wstring()));
        HANDLE singleton=CreateMutexW(nullptr,FALSE,mutex_name.c_str());
        struct Close { HANDLE h; ~Close(){if(h) CloseHandle(h);} } close{singleton};
        if(!singleton) throw std::runtime_error("Cannot create the launcher lock.");
        if(GetLastError()==ERROR_ALREADY_EXISTS && !app.preview) {
            MessageBoxW(nullptr,app.label(Text::already_running),L"N.O.V.A. 3",MB_OK); return 0;
        }
        if(!app.preview && !setup && ready(app.layout)) {
            try {create_game_shortcut(app.layout,app.launcher);} catch(const std::exception &) { /* Optional shell convenience must not prevent play. */ }
            return wait_game(start_game(app.layout,app.executable),app.language);
        }
        app.appearance();
        WNDCLASSW cls{}; cls.lpfnWndProc=procedure; cls.hInstance=instance;
        cls.lpszClassName=L"Nova3FirstRun"; cls.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
        // Taskbar/Alt+Tab use the executable's icon; WS_EX_DLGMODALFRAME keeps the caption icon-free.
        cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1)); RegisterClassW(&cls);
        auto screen=GetDC(nullptr); app.dpi=GetDeviceCaps(screen,LOGPIXELSX); ReleaseDC(nullptr,screen);
        
        RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
        // Client area keeps the logical canvas proportions; the native caption is added around it.
        int width=std::min<int>({app.px(canvas_width),static_cast<int>(work.right-work.left-60),
            static_cast<int>((work.bottom-work.top-60)*canvas_width/canvas_height)});
        RECT frame{0,0,width,width*canvas_height/canvas_width};
        AdjustWindowRectEx(&frame,WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE,WS_EX_APPWINDOW|WS_EX_DLGMODALFRAME);
        width=frame.right-frame.left; const int height=frame.bottom-frame.top;
        app.window=CreateWindowExW(WS_EX_APPWINDOW|WS_EX_DLGMODALFRAME,cls.lpszClassName,L"N.O.V.A. 3",WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
            work.left+(work.right-work.left-width)/2,work.top+(work.bottom-work.top-height)/2,width,height,nullptr,nullptr,instance,&app);
        if(!app.window) throw std::runtime_error("Cannot create setup window.");
        BOOL dark=TRUE; DwmSetWindowAttribute(app.window,20,&dark,sizeof(dark));
        COLORREF caption=RGB(0,0,0); DwmSetWindowAttribute(app.window,35,&caption,sizeof(caption));
        COLORREF border=RGB(24,24,24),caption_text=RGB(235,235,235);
        DwmSetWindowAttribute(app.window,34,&border,sizeof(border));
        DwmSetWindowAttribute(app.window,36,&caption_text,sizeof(caption_text));
        if(app.preview) {
            app.preview_progress=std::wstring(command)==L"--preview-progress";
            const bool progress=app.preview_progress;app.apk_ready=progress;
            app.headline=app.label(progress?Text::preparing:Text::welcome);
            app.description=app.label(progress?Text::auto_start:Text::apk_help);
            app.stage=app.label(progress?Text::extracting:Text::waiting_apk); app.percent=progress?64:0;
            append_log(app,"Ready for your original game files.");
            if(progress) {
                append_log(app,"Checking APK compatibility"); append_log(app,"Importing the game library");
                append_log(app,"Verifying main / patch OBB"); append_log(app,"Preparing 26/40: sounds_hi.gla");
                SetWindowTextW(app.select,app.label(Text::busy)); EnableWindow(app.select,FALSE);
            }
        } else {
            app.headline=app.label(Text::welcome);
            app.description=app.label(Text::apk_help);
            app.stage=app.label(Text::waiting_apk);
        }
        layout(app);
        ShowWindow(app.window,SW_SHOWNORMAL); UpdateWindow(app.window);
        // Preparation starts only after the user's selection, never on window open.
        if(!app.preview)SetFocus(app.select);
        MSG message{};
        while(GetMessageW(&message,nullptr,0,0)>0) {
            if(!IsDialogMessageW(app.window,&message)) {TranslateMessage(&message); DispatchMessageW(&message);}
        }
        return app.game ? wait_game(app.game,app.language) : app.error?1:0;
    } catch(const std::exception &e) {
        MessageBoxW(nullptr,widen(e.what()).c_str(),L"N.O.V.A. 3 setup",MB_OK|MB_ICONERROR); return 1;
    }
}
