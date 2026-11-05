#pragma once
#include "abi/guest_hook.h"
#include "input/controls.h"
#include <memory>
#include <optional>

namespace nova::input {
// Keyboard edges supplement original button queries. CLevel::Update still owns
// multiplayer/availability checks and the complete native screen lifecycle.
class NativeCommunications {
public:
    struct Bindings { uint32_t get_level, application, current_state, ingame_vtable, scores_vtable, chat_vtable, get_parent; };
    NativeCommunications(const ElfImage &,Controls &);
    // The same installed adapter with isolated ownership providers for fixtures.
    NativeCommunications(const ElfImage &,Controls &,Bindings);
    ~NativeCommunications();
    void begin_frame();
    void end_frame() { chat_=scores_=false; level_=0; }
    bool press_score(uint32_t button) noexcept;
    std::optional<uint32_t> panel_close(uint32_t button) noexcept;
    bool is_panel(uint32_t state) const;
    bool gameplay_state(uint32_t state) const;
private:
    static NativeCommunications *instance_;
    static uint32_t __cdecl chat_pressed(uint32_t button) noexcept;
    static void __cdecl chat_draw(uint32_t button,uint32_t alpha) noexcept;
    static void __cdecl button_draw(uint32_t button,uint32_t alpha) noexcept;
    static void __cdecl chat_paint(uint32_t sprite,uint32_t frame,uint32_t x,uint32_t y,
                                  uint32_t flags,uint32_t sx,uint32_t sy,uint32_t alpha) noexcept;
    uint32_t current_state() const;
    bool is_resume(uint32_t button) const;
    bool press(uint32_t button,uint32_t offset,bool &pending);
    Controls &input_;
    Bindings bindings_;
    std::unique_ptr<abi::GuestHook> chat_hook_;
    std::unique_ptr<abi::GuestHook> chat_draw_hook_,button_draw_hook_;
    std::unique_ptr<abi::GuestCall> chat_idle_paint_,chat_active_paint_;
    uint32_t level_=0;
    uint64_t epoch_=0;
    bool chat_=false,scores_=false;
};
}
