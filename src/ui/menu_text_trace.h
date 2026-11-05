#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <mutex>

namespace nova::ui {
// Provisional, bounded menu-only observation. No guest calls or layout changes.
class MenuTextTrace {
public:
    struct Glyph {
        uint32_t font=0,size=0,path=0,outline_26_6=0;
        uint32_t low_width=0,low_height=0,high_width=0,high_height=0;
        uint32_t target_width=0,target_height=0,target_density=0;
        bool available=false,selected=false,monochrome=false;
    };
    struct Record {
        std::array<char,81> text{};
        uint32_t caller=0,font=0;
        bool ttf=false;
        Glyph first{};
        unsigned glyphs=0,dense=0,missing=0,monochrome=0,rectangles=0;
        uint32_t destination_width=0,destination_height=0;
    };
    class Scope {
    public:
        Scope(bool menu,bool ttf,uint32_t font,const char* text,uint32_t caller);
        ~Scope();
        Scope(const Scope&)=delete;
        Scope& operator=(const Scope&)=delete;
    private:
        Record record_{};
        Record* previous_=nullptr;
        bool active_=false;
    };
    static void configure(bool enabled);
    static bool active() { return current_!=nullptr; }
    static void glyph(Glyph sample);
    static void rectangle(uint32_t width=0,uint32_t height=0);
    static void report(FILE* output);
    static constexpr unsigned capacity=256;
private:
    static void append(const Record& record);
    inline static thread_local Record* current_=nullptr;
    inline static std::mutex mutex_;
    inline static bool enabled_=false;
    inline static unsigned count_=0,reported_=0;
    static std::array<Record,capacity> records_;
};
}
