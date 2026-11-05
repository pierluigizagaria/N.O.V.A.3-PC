#pragma once
#include "launcher/language.h"
#include <windows.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
namespace nova::launcher {
// Control identifiers shared by the window procedure and the painter.
inline constexpr int select_id=101,retry_id=103,language_first_id=110;
struct SkinState {
    Language language=Language::english;
    std::wstring heading,description,operation,select_label;
    std::wstring apk_name,data_name;          // User selections shown in the step rail.
    std::vector<std::wstring> log;
    unsigned percent=0;
    bool processing=false,apk_ready=false,data_ready=false,want_data=false;
    bool error=false,retry=false,drop_hover=false,focus_cues=true;
    int focused=0,hover=0,pressed=0;
};
// Logical client coordinates on an 800x540 canvas; native Windows owns the title bar.
inline constexpr int canvas_width=800,canvas_height=540;
struct SkinRect {int x,y,w,h; bool empty() const {return w<=0||h<=0;}};
struct SkinControls {SkinRect select,retry,language[language_count];};
// Visible controls for a state; empty rectangles are hidden.
SkinControls skin_controls(const SkinState &);
inline constexpr size_t visible_log_rows=4;

class Skin {
public:
    Skin();
    ~Skin();
    Skin(const Skin &)=delete;
    Skin &operator=(const Skin &)=delete;
    void load(const std::filesystem::path &root);
    bool original_font() const;
    bool complete() const;
    HICON icon() const;
    void draw(HDC,int width,int height,const SkinState &,int offset_x=0,int offset_y=0);
    void preview(const std::filesystem::path &,const SkinState &,int width=canvas_width,int height=canvas_height);
    std::string audit() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
