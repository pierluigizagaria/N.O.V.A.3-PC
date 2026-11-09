// One context/mutex; platform effects are applied by the UI owner after Render.
#include "platform/fov_panel.h"
#include "movement_wobble/integration_gate.h"
#include "weapon_placement/position_gate.h"
#include <GLES3/gl3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#include <array>
#include <algorithm>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <stdexcept>
#include <windows.h>

namespace nova::platform {
namespace {
// Guard is used before every renderer call, including init/prewarm/NewFrame
// resource recreation. Do not rely on the toolkit's draw-only backup.
class GlState {
    GLint program_=0,active_=0,texture0_=0,sampler0_=0,array_=0,element_=0,vao_=0;
    GLint draw_=0,read_=0,unpack_buffer_=0;
    std::array<GLint,4> viewport_{},scissor_{},unpack_{};
    std::array<GLboolean,4> color_{};
    std::array<GLint,6> blend_{};
    std::array<GLboolean,9> enables_{};
    static constexpr std::array<GLenum,4> unpack_names_{
        GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS};
    static constexpr std::array<GLenum,6> blend_names_{
        GL_BLEND_SRC_RGB,GL_BLEND_DST_RGB,GL_BLEND_SRC_ALPHA,GL_BLEND_DST_ALPHA,
        GL_BLEND_EQUATION_RGB,GL_BLEND_EQUATION_ALPHA};
    static constexpr std::array<GLenum,9> enable_names_{
        GL_BLEND,GL_CULL_FACE,GL_DEPTH_TEST,GL_STENCIL_TEST,GL_SCISSOR_TEST,
        GL_PRIMITIVE_RESTART_FIXED_INDEX,GL_RASTERIZER_DISCARD,GL_SAMPLE_COVERAGE,
        GL_SAMPLE_ALPHA_TO_COVERAGE};
  public:
    GlState() {
        glGetIntegerv(GL_CURRENT_PROGRAM,&program_);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active_); glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture0_);
        glGetIntegerv(GL_SAMPLER_BINDING,&sampler0_);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&array_);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&element_);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao_);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw_);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read_);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack_buffer_);
        glGetIntegerv(GL_VIEWPORT,viewport_.data());
        glGetIntegerv(GL_SCISSOR_BOX,scissor_.data());
        glGetBooleanv(GL_COLOR_WRITEMASK,color_.data());
        for(size_t i=0;i<unpack_.size();++i) glGetIntegerv(unpack_names_[i],&unpack_[i]);
        for(size_t i=0;i<blend_.size();++i) glGetIntegerv(blend_names_[i],&blend_[i]);
        for(size_t i=0;i<enables_.size();++i) enables_[i]=glIsEnabled(enable_names_[i]);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        for(size_t i=1;i<unpack_.size();++i) glPixelStorei(unpack_names_[i],0);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glDisable(GL_RASTERIZER_DISCARD);
        glDisable(GL_SAMPLE_COVERAGE);
        glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    }
    ~GlState() {
        glUseProgram(static_cast<GLuint>(program_));
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(texture0_));
        glBindSampler(0,static_cast<GLuint>(sampler0_)); glActiveTexture(static_cast<GLenum>(active_));
        glBindVertexArray(static_cast<GLuint>(vao_));
        glBindBuffer(GL_ARRAY_BUFFER,static_cast<GLuint>(array_));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,static_cast<GLuint>(element_));
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,static_cast<GLuint>(unpack_buffer_));
        for(size_t i=0;i<unpack_.size();++i) glPixelStorei(unpack_names_[i],unpack_[i]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,static_cast<GLuint>(draw_));
        glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(read_));
        glViewport(viewport_[0],viewport_[1],viewport_[2],viewport_[3]);
        glScissor(scissor_[0],scissor_[1],scissor_[2],scissor_[3]);
        glColorMask(color_[0],color_[1],color_[2],color_[3]);
        glBlendFuncSeparate(static_cast<GLenum>(blend_[0]),static_cast<GLenum>(blend_[1]),
            static_cast<GLenum>(blend_[2]),static_cast<GLenum>(blend_[3]));
        glBlendEquationSeparate(static_cast<GLenum>(blend_[4]),static_cast<GLenum>(blend_[5]));
        for(size_t i=0;i<enables_.size();++i)
            if(enables_[i]) glEnable(enable_names_[i]); else glDisable(enable_names_[i]);
    }
};
void rectangle(FovUiRect &out) {
    const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
    out={a.x,a.y,b.x,b.y};
}
// ImGui has directional ArrowButton, but no circular reset icon. Keep normal
// button interaction/focus and draw the icon independently of the font atlas.
bool reset_button(const char *id,const char *tooltip,float scale) {
    const float size=ImGui::GetFrameHeight();
    const bool pressed=ImGui::Button(id,{size,size});
    const auto origin=ImGui::GetItemRectMin();
    const ImVec2 center{origin.x+size*.5f,origin.y+size*.5f};
    const float radius=size*.27f,end=5.6f;
    auto *draw=ImGui::GetWindowDrawList();
    const auto color=ImGui::GetColorU32(ImGuiCol_Text);
    draw->PathArcTo(center,radius,.5f,end,24);
    draw->PathStroke(color,0,1.6f*scale);
    const ImVec2 tip{center.x+radius*std::cos(end),center.y+radius*std::sin(end)};
    const ImVec2 tangent{-std::sin(end),std::cos(end)};
    const ImVec2 normal{std::cos(end),std::sin(end)};
    draw->AddTriangleFilled({tip.x+tangent.x*3*scale,tip.y+tangent.y*3*scale},
        {tip.x-tangent.x*2*scale+normal.x*3*scale,tip.y-tangent.y*2*scale+normal.y*3*scale},
        {tip.x-tangent.x*2*scale-normal.x*3*scale,tip.y-tangent.y*2*scale-normal.y*3*scale},color);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",tooltip);
    return pressed;
}
bool row(const char *name,std::optional<float> &value,float &seed,float maximum,
         FovUiRect &rect,FovUiRect &reset_rect,float default_value,bool &invalid,
         bool &edit_pending,bool &active_edit,bool &reset,float scale) {
    ImGui::PushID(name);
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(name);
    ImGui::SameLine(130*scale);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x-ImGui::GetFrameHeight()-ImGui::GetStyle().ItemSpacing.x);
    if(value) seed=*value;
    const auto previous_seed=seed;
    if(ImGui::SliderFloat("##value",&seed,60,maximum,"%.1f",ImGuiSliderFlags_AlwaysClamp)) {
        invalid=!std::isfinite(seed) || seed<60 || seed>maximum;
        if(invalid) seed=previous_seed;
        else { value=seed; edit_pending=true; }
    }
    rectangle(rect);
    active_edit|=ImGui::IsItemActive();
    bool completed=ImGui::IsItemDeactivatedAfterEdit() && edit_pending && !invalid;
    if(completed) edit_pending=false;
    ImGui::SameLine();
    if(reset_button("##reset","Reset to default",scale)) {
        value.reset(); seed=default_value; invalid=false;
        edit_pending=false; completed=true; reset=true;
    }
    rectangle(reset_rect);
    ImGui::PopID();
    return completed;
}
} // namespace
FovPanel::FovPanel(camera::NativeWorldFov::Options initial,input::ControlsConfig bindings,movement_wobble::Strength movement,int ui_scale)
    :options_(initial),movement_wobble_seed_(movement.percent()),ui_scale_seed_(ui_scale),bindings_(std::move(bindings)),
     sensitivity_edit_({bindings_.sensitivity,bindings_.aim_multiplier}) {
    const auto baseline=*camera::native_fov_defaults();
    world_seed_=baseline.world; weapon_seed_=baseline.weapon;
    if(initial.world) world_seed_=*initial.world;
    if(initial.weapon) weapon_seed_=*initial.weapon;
}
void FovPanel::initialize_weapon_position(weapon_placement::Position position) {
    if(context_) throw std::logic_error("Initialize weapon position before UI initialization");
    if(!weapon_position_requested_.set(position)) throw std::invalid_argument("Invalid weapon position pair");
    weapon_position_draft_=weapon_position_requested_.requested();
}
FovPanel::~FovPanel() { assert(!context_ && !renderer_initialized_ && !platform_initialized_); }
void FovPanel::select() const { ImGui::SetCurrentContext(context_); }
void FovPanel::require_ui() const {
    if(ui_lane_!=GetCurrentThreadId()) throw std::logic_error("Panel operation outside UI lane");
}
void FovPanel::require_gl() const {
    if(gl_lane_!=GetCurrentThreadId()) throw std::logic_error("Panel operation outside GL lane");
}
void FovPanel::ui_initialize(SDL_Window *window,PanelBackendEffects *effects) {
    ui_lane_=GetCurrentThreadId(); window_=window;
    effects_=effects ? effects : &default_effects_;
    effects_->bind();
    std::lock_guard lock(mutex_);
    IMGUI_CHECKVERSION(); context_=ImGui::CreateContext(); select();
    auto &io=ImGui::GetIO(); io.IniFilename=nullptr; io.LogFilename=nullptr;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    io.MouseDrawCursor=false;
    if(!ImGui_ImplSDL3_InitForOther(window)) {
        ImGui::DestroyContext(context_); context_=nullptr;
        effects_->unbind(); effects_=nullptr;
        throw std::runtime_error("Cannot initialize ImGui SDL3 platform backend");
    }
    // Auto gamepad enumeration/opening is outside this two-setting panel.
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual,nullptr,0);
    platform_initialized_=true;
    ImGui::StyleColorsDark();
    auto &style=ImGui::GetStyle();
    style.WindowRounding=4; style.FrameRounding=3;
    style.WindowPadding={20,18}; style.ItemSpacing={10,10};
    style.Colors[ImGuiCol_WindowBg]={0.055f,0.075f,0.09f,0.98f};
    style.Colors[ImGuiCol_Text]={0.96f,0.98f,1.f,1.f};
}
void FovPanel::gl_initialize(bool fail_after_prewarm) {
    gl_lane_=GetCurrentThreadId();
    std::lock_guard lock(mutex_); select(); GlState state;
    if(!ImGui_ImplOpenGL3_Init("#version 300 es"))
        throw std::runtime_error("Cannot initialize ImGui GLES3 renderer");
    renderer_initialized_=true;
    if(!ImGui_ImplOpenGL3_CreateDeviceObjects()) {
        ImGui_ImplOpenGL3_Shutdown(); renderer_initialized_=false;
        throw std::runtime_error("Cannot prewarm ImGui GLES3 resources");
    }
    if(fail_after_prewarm) {
        ImGui_ImplOpenGL3_Shutdown(); renderer_initialized_=false;
        throw std::runtime_error("Injected renderer prewarm failure");
    }
    renderer_ready_=true; // No wait/guest/O01 work under this lock.
}
void FovPanel::gl_shutdown() {
    require_gl(); std::lock_guard lock(mutex_); select();
    drawable_=false; renderer_ready_=false;
    if(renderer_initialized_) { GlState state; ImGui_ImplOpenGL3_Shutdown(); renderer_initialized_=false; }
}
void FovPanel::ui_shutdown() {
    require_ui(); std::lock_guard lock(mutex_);
    if(!context_) return;
    if(renderer_initialized_) throw std::logic_error("GL shutdown must precede UI shutdown");
    select();
    if(platform_initialized_) ImGui_ImplSDL3_Shutdown();
    platform_initialized_=false; ImGui::DestroyContext(context_); context_=nullptr;
    effects_->unbind(); effects_=nullptr;
}
void FovPanel::ui_visible(bool visible) {
    require_ui(); std::lock_guard lock(mutex_); select();
    if(!visible && visible_) {
        if(edit_pending_ && !world_invalid_ && !weapon_invalid_) save_pending_=true;
        edit_pending_=false;
    }
    visible_=visible;
    release_input_locked();
}
void FovPanel::ui_window_transition() {
    require_ui(); std::lock_guard lock(mutex_); select();
    // A mode change ends the current edit just like an explicit close. Its
    // last valid pair is finalized once; held physical ups stay quarantined.
    if(edit_pending_ && !world_invalid_ && !weapon_invalid_) save_pending_=true;
    edit_pending_=false;
    release_input_locked();
}
void FovPanel::release_input_locked() {
    binding_capture_.reset();
    release_backend_input_locked();
}
void FovPanel::release_backend_input_locked() {
    sensitivity_edit_.finish();
    movement_save_pending_|=movement_edit_pending_; movement_edit_pending_=false;
    drawable_=false; ++ui_serial_;
    ImGui::ClearActiveID(); ImGui::GetIO().ClearInputKeys();
    ImGui::GetIO().ClearInputMouse(); // Keys and mouse are separate in pinned ImGui.
    // Ownership transitions consume physical ups, so also retire the pinned
    // backend's private MouseButtonsDown bookkeeping with records only.
    for(Uint8 button : {Uint8(SDL_BUTTON_LEFT),Uint8(SDL_BUTTON_RIGHT),Uint8(SDL_BUTTON_MIDDLE),
                       Uint8(SDL_BUTTON_X1),Uint8(SDL_BUTTON_X2)}) {
        SDL_Event release{}; release.type=SDL_EVENT_MOUSE_BUTTON_UP;
        release.button.windowID=SDL_GetWindowID(window_); release.button.button=button;
        ImGui_ImplSDL3_ProcessEvent(&release);
    }
    effects_->clear_requests();
}
void FovPanel::invalidate_draw() {
    require_ui(); std::lock_guard lock(mutex_); drawable_=false; ++ui_serial_;
}
bool FovPanel::ui_binding_capture() {
    require_ui(); std::lock_guard lock(mutex_); return binding_capture_.active();
}
void FovPanel::ui_event(const SDL_Event &event) {
    require_ui(); std::lock_guard lock(mutex_); select();
    if(!visible_) return;
    if(binding_capture_.active() && event.type==SDL_EVENT_MOUSE_BUTTON_DOWN &&
       event.button.button==SDL_BUTTON_LEFT) {
        const auto hit=[&](const FovUiRect &r) {
            return r.x2>r.x1 && r.y2>r.y1 && event.button.x>=r.x1 && event.button.x<r.x2 &&
                event.button.y>=r.y1 && event.button.y<r.y2;
        };
        // Keep Cancel, normal X/title dragging and tab navigation usable. Their
        // click still belongs exclusively to the panel, never to gameplay.
        if(hit(binding_cancel_rect_)) {
            binding_capture_.cancel_mouse(event.button.button);
            return; // Do not activate a row exposed when the Cancel UI disappears.
        }
        if(hit(title_rect_) || hit(controls_tab_rect_) ||
           hit(display_controls_[static_cast<size_t>(DisplayControl::gameplay_tab)]) ||
           hit(display_controls_[static_cast<size_t>(DisplayControl::display_tab)]) ||
           hit(display_controls_[static_cast<size_t>(DisplayControl::graphics_tab)]))
            binding_capture_.cancel();
    }
    if(!binding_capture_.event(event,bindings_)) ImGui_ImplSDL3_ProcessEvent(&event);
}
void FovPanel::initialize_graphics_quality(GraphicsQuality quality) {
    if(context_ || !valid_graphics_quality(quality)) throw std::invalid_argument("Invalid initial graphics quality");
    graphics_startup_=graphics_selected_=quality;
}
FovCommands FovPanel::ui_frame(const FovUiFrame &frame) {
    require_ui();
    std::lock_guard lock(mutex_); select();
    FovCommands command; command.visible=options_; command.visible_revision=visible_revision_;
    if(ui_scale_pending_ && (!frame.focused || frame.minimized || !visible())) {
        command.ui_scale=ui_scale_seed_; command.save_ui_scale=true; ui_scale_pending_=false;
    }
    if(!frame.focused || frame.minimized) {
        binding_capture_.reset(); sensitivity_edit_.finish();
        movement_save_pending_|=movement_edit_pending_; movement_edit_pending_=false;
    }
    if(movement_save_pending_) {
        command.movement_wobble=movement_wobble::Strength::from_percent(movement_wobble_seed_);
        movement_save_pending_=false;
    }
    command.sensitivity=sensitivity_edit_.take();
    if(binding_capture_.take_completed()) command.bindings=bindings_;
    if(!renderer_ready_ || !presentable_ || frame.minimized) {
        drawable_=false;
        // A hidden finalized close may flush; an open editor must keep its
        // completed-pair request pending across no-swap/minimized frames.
        if(!visible_ && !edit_pending_) { command.save=save_pending_; save_pending_=false; }
        return command;
    }
    command.save=save_pending_; save_pending_=false;
    const bool open=visible_;
    // Closed hints bypass platform NewFrame, so it cannot capture/show the
    // cursor or steal guest text. Host arbiter normalizes side effects after
    // every open backend frame and explicitly restores guest text on close.
    if(open) ImGui_ImplSDL3_NewFrame();
    auto &io=ImGui::GetIO();
    if(open) io.AddFocusEvent(frame.focused);
    io.DisplaySize={frame.width,frame.height};
    io.DisplayFramebufferScale={frame.framebuffer_scale_x,frame.framebuffer_scale_y};
    io.DeltaTime=frame.delta_seconds>0 ? frame.delta_seconds : 1.f/60.f;
    const float scale=std::clamp(frame.dpi_scale,0.75f,3.f);
    io.FontGlobalScale=1.25f*scale;
    if(!open) { io.AddMousePosEvent(-FLT_MAX,-FLT_MAX); io.AddFocusEvent(false); }
    ImGui::NewFrame();
    if(open) {
        bool stay_open=true;
        bool active_edit=false,reset=false;
        const auto previous=options_;
        const auto defaults=camera::native_fov_defaults(frame.guest_aspect);
        if(defaults) {
            if(!options_.world) world_seed_=defaults->world;
            if(!options_.weapon) weapon_seed_=defaults->weapon;
        }
        const float error_height=frame.last_save.error.empty() && frame.display_error.empty() && frame.render_error.empty() ? 0.f : 42.f;
        const float movement_height=movement_wobble::kControlAvailable && movement_gameplay_tab_active_
            ? 32.f+(frame.movement_wobble_error.empty()?0.f:64.f) : 0.f;
        const float position_height=weapon_placement::kPositionControlAvailable && position_gameplay_tab_active_
            ? 156.f+(frame.weapon_position_error.empty()?0.f:66.f) : 0.f;
        ImGui::SetNextWindowSize({std::min((controls_tab_active_ ? 540.f : 460.f)*scale,frame.width-48),
                                  std::min((controls_tab_active_ ? 520.f : display_tab_active_ ? 300+error_height : graphics_tab_active_ ? 240.f+(frame.graphics_quality_error.empty()?0.f:80.f) : 210+error_height+movement_height+position_height)*scale,frame.height-80)},ImGuiCond_Always);
        ImGui::SetNextWindowPos({24,54},ImGuiCond_FirstUseEver);
        if(ImGui::Begin("Settings",&stay_open,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse)) {
            const auto window_pos=ImGui::GetWindowPos(),window_size=ImGui::GetWindowSize();
            title_rect_={window_pos.x,window_pos.y,window_pos.x+window_size.x,
                         window_pos.y+ImGui::GetFrameHeight()};
            controls_tab_active_=false;
            if(ImGui::BeginTabBar("Settings tabs")) {
                const bool gameplay_tab=ImGui::BeginTabItem("Gameplay");
                movement_gameplay_tab_active_=gameplay_tab;
                position_gameplay_tab_active_=gameplay_tab;
                rectangle(display_controls_[static_cast<size_t>(DisplayControl::gameplay_tab)]);
                if(gameplay_tab) {
                    sensitivity_edit_.finish();
                    binding_capture_.cancel();
                    command.save|=row("FOV",options_.world,world_seed_,120,controls_[0],
                        controls_[static_cast<size_t>(FovControl::world_reset)],defaults ? defaults->world : world_seed_,
                        world_invalid_,edit_pending_,active_edit,reset,scale);
                    command.save|=row("Weapon FOV",options_.weapon,weapon_seed_,104,controls_[1],
                        controls_[static_cast<size_t>(FovControl::weapon_reset)],defaults ? defaults->weapon : weapon_seed_,
                        weapon_invalid_,edit_pending_,active_edit,reset,scale);
                    // No row is emitted while the real native binding is absent.
                    if constexpr(movement_wobble::kControlAvailable) {
                        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Camera bob");
                        ImGui::SameLine(130*scale);
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x-ImGui::GetFrameHeight()-ImGui::GetStyle().ItemSpacing.x);
                        if(ImGui::SliderInt("##movement_wobble",&movement_wobble_seed_,0,100,"%d%%",ImGuiSliderFlags_AlwaysClamp))
                            movement_edit_pending_=true;
                        rectangle(controls_[static_cast<size_t>(FovControl::movement_slider)]);
                        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Camera motion while walking or sprinting. 0%% = off, 100%% = original.");
                        if(ImGui::IsItemDeactivatedAfterEdit() && movement_edit_pending_) {
                            command.movement_wobble=movement_wobble::Strength::from_percent(movement_wobble_seed_);
                            movement_edit_pending_=false;
                        }
                        ImGui::SameLine();
                        if(reset_button("##reset_bob","Reset camera bob",scale)) {
                            movement_wobble_seed_=movement_wobble::kResetPercent;
                            movement_edit_pending_=movement_save_pending_=false;
                            command.movement_wobble=movement_wobble::Strength{};
                        }
                        rectangle(controls_[static_cast<size_t>(FovControl::movement_reset)]);
                        if(!frame.movement_wobble_error.empty()) {
                            ImGui::TextWrapped("Could not save movement wobble: %s",frame.movement_wobble_error.c_str());
                            if(ImGui::Button("Retry movement save"))
                                command.movement_wobble=movement_wobble::Strength::from_percent(movement_wobble_seed_);
                        }
                    }
                    if constexpr(weapon_placement::kPositionControlAvailable) {
                        auto position_row=[&](const char *label,float &coordinate) {
                            float percent=coordinate*100.f;
                            ImGui::TextUnformatted(label);
                            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x-ImGui::GetFrameHeight()-ImGui::GetStyle().ItemSpacing.x);
                            ImGui::PushID(label);
                            if(ImGui::SliderFloat("##position",&percent,-5.f,5.f,"%+.2f%%",ImGuiSliderFlags_AlwaysClamp)) {
                                const auto fraction=percent/100.f;
                                if(weapon_placement::valid({fraction,0})) coordinate=fraction;
                            }
                            ImGui::SameLine();
                            if(reset_button("##reset_position","Reset weapon position",scale)) coordinate=0;
                            ImGui::PopID();
                        };
                        position_row("Weapon position X (right +)",weapon_position_draft_.x);
                        position_row("Weapon position Y (up +)",weapon_position_draft_.y);
                        if(ImGui::Button("Apply position") && weapon_position_requested_.set(weapon_position_draft_))
                            command.weapon_position=weapon_position_requested_.requested();
                        ImGui::SameLine(); ImGui::TextUnformatted("0% = Original");
                        if(!frame.weapon_position_error.empty()) {
                            ImGui::TextWrapped("%s",frame.weapon_position_error.c_str());
                            if(ImGui::Button("Retry position save"))
                                command.weapon_position=weapon_position_requested_.requested();
                        }
                    }
                    ImGui::EndTabItem();
                }
                const bool display_tab=ImGui::BeginTabItem("Display");
                rectangle(display_controls_[static_cast<size_t>(DisplayControl::display_tab)]);
                if(display_tab) {
                    display_tab_active_=true;
                    sensitivity_edit_.finish();
                    binding_capture_.cancel();
                    // The old row is not submitted on this tab, so its normal
                    // deactivation callback cannot finalize a valid edit.
                    if(edit_pending_ && !world_invalid_ && !weapon_invalid_) {
                        command.save=true; edit_pending_=false;
                    }
                    auto display=frame.display;
                    const auto selected=frame.render_extent;
                    const auto label=render_extent_name(selected)+(selected!=frame.active_render_extent ? " *" : "");
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Resolution");
                    ImGui::SameLine(130*scale); ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::BeginDisabled(!frame.render_available || frame.render_busy || frame.render_confirming);
                    const bool combo=ImGui::BeginCombo("##resolution",label.c_str());
                    rectangle(display_controls_[static_cast<size_t>(DisplayControl::resolution)]);
                    if(combo) {
                        for(const auto &entry:frame.render_choices) {
                            const auto choice=entry.extent;
                            const bool current=choice==selected;
                            auto name=render_extent_name(choice);
                            if(entry.recommended) name+=" (Recommended)";
                            if(ImGui::Selectable(name.c_str(),current) &&
                               (!current || selected!=frame.active_render_extent || frame.render_unsaved)) {
                                command.render_extent=choice; command.apply_render=true;
                            }
                            if(choice==GuestExtent{1600,900})
                                rectangle(display_controls_[static_cast<size_t>(DisplayControl::first_resolution)]);
                            if(choice==GuestExtent{1920,1080})
                                rectangle(display_controls_[static_cast<size_t>(DisplayControl::full_hd_resolution)]);
                            if(current) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::EndDisabled();
                    if(ImGui::Checkbox("Fullscreen",&display.fullscreen)) {
                        display.fullscreen_resolution={}; command.display=display;
                    }
                    rectangle(display_controls_[static_cast<size_t>(DisplayControl::fullscreen)]);
                    if(!frame.render_error.empty()) ImGui::TextWrapped("%s",frame.render_error.c_str());
                    if(!frame.display_error.empty()) ImGui::TextWrapped("%s",frame.display_error.c_str());
                    ImGui::Separator();
                    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("HUD scale");
                    ImGui::SameLine(130*scale);
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x-ImGui::GetFrameHeight()-ImGui::GetStyle().ItemSpacing.x);
                    if(ImGui::SliderInt("##ui_scale",&ui_scale_seed_,ui::minimum_percent,ui::maximum_percent,"%d%%",ImGuiSliderFlags_AlwaysClamp)) {
                        command.ui_scale=ui_scale_seed_; ui_scale_pending_=true;
                    }
                    rectangle(display_controls_[static_cast<size_t>(DisplayControl::ui_scale)]);
                    if(ImGui::IsItemHovered()) ImGui::SetTooltip("100%% preserves native HUD size. Main and pause menus keep their original layout.");
                    if(ImGui::IsItemDeactivatedAfterEdit() && ui_scale_pending_) {
                        command.ui_scale=ui_scale_seed_; command.save_ui_scale=true; ui_scale_pending_=false;
                    }
                    ImGui::SameLine();
                    if(reset_button("##reset_hud","Reset HUD scale",scale)) {
                        ui_scale_seed_=ui::default_percent; command.ui_scale=ui_scale_seed_;
                        command.save_ui_scale=true; ui_scale_pending_=false;
                    }
                    rectangle(display_controls_[static_cast<size_t>(DisplayControl::ui_scale_reset)]);
                    if(!frame.ui_scale_error.empty()) {
                        ImGui::TextWrapped("Could not save UI scale: %s",frame.ui_scale_error.c_str());
                        if(ImGui::Button("Retry UI scale save")) { command.ui_scale=ui_scale_seed_; command.save_ui_scale=true; }
                    }
                    ImGui::EndTabItem();
                }
                else {
                    display_tab_active_=false;
                    if(ui_scale_pending_) { command.ui_scale=ui_scale_seed_; command.save_ui_scale=true; ui_scale_pending_=false; }
                }
                const bool graphics_tab=ImGui::BeginTabItem("Graphics");
                rectangle(display_controls_[static_cast<size_t>(DisplayControl::graphics_tab)]);
                graphics_tab_active_=graphics_tab;
                if(graphics_tab) {
                    binding_capture_.cancel();
                    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Detail distance");
                    ImGui::SameLine(190*scale); ImGui::SetNextItemWidth(-FLT_MIN);
                    const char *distances[]={"1x (Original)","2x","3x","4x (Best)"};
                    const auto distance_label=std::string(distances[graphics_selected_.lod_distance-1])+
                        (graphics_selected_.lod_distance!=graphics_startup_.lod_distance ? " *" : "");
                    if(ImGui::BeginCombo("##lod_distance",distance_label.c_str())) {
                        for(int i=1;i<=4;++i) {
                            if(ImGui::Selectable(distances[i-1],graphics_selected_.lod_distance==i)) {
                                graphics_selected_.lod_distance=i;command.graphics_quality=graphics_selected_;
                            }
                            if(i==1) rectangle(display_controls_[static_cast<size_t>(DisplayControl::lod_original)]);
                            if(i==4) rectangle(display_controls_[static_cast<size_t>(DisplayControl::lod_best)]);
                        }
                        ImGui::EndCombo();
                    }
                    rectangle(display_controls_[static_cast<size_t>(DisplayControl::lod_distance)]);
                    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Multiplier of the original camera-to-object detail thresholds.");
                    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Texture filtering");
                    ImGui::SameLine(190*scale); ImGui::SetNextItemWidth(-FLT_MIN);
                    const char *textures[]={"Original","Trilinear","Trilinear + 16x"};
                    const auto texture_label=std::string(textures[graphics_selected_.texture_filtering])+
                        (graphics_selected_.texture_filtering!=graphics_startup_.texture_filtering ? " *" : "");
                    if(ImGui::BeginCombo("##texture_filtering",texture_label.c_str())) {
                        for(int i=0;i<3;++i) {
                            if(ImGui::Selectable(textures[i],graphics_selected_.texture_filtering==i)) {
                                graphics_selected_.texture_filtering=i;command.graphics_quality=graphics_selected_;
                            }
                            if(i==0) rectangle(display_controls_[static_cast<size_t>(DisplayControl::texture_original)]);
                            if(i==2) rectangle(display_controls_[static_cast<size_t>(DisplayControl::texture_best)]);
                        }
                        ImGui::EndCombo();
                    }
                    rectangle(display_controls_[static_cast<size_t>(DisplayControl::texture_filtering)]);
                    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Anisotropy uses up to 16x, limited by your GPU. Original texture resolution is preserved.");
                    if(graphics_selected_!=graphics_startup_) {
                        ImGui::Spacing(); ImGui::TextUnformatted("Restart required");
                    }
                    if(!frame.graphics_quality_error.empty()) {
                        ImGui::TextWrapped("Could not save graphics quality: %s",frame.graphics_quality_error.c_str());
                        if(ImGui::Button("Retry quality save")) command.graphics_quality=graphics_selected_;
                        rectangle(display_controls_[static_cast<size_t>(DisplayControl::graphics_retry)]);
                    }
                    ImGui::EndTabItem();
                }
                const bool controls_tab=ImGui::BeginTabItem("Controls");
                rectangle(controls_tab_rect_);
                if(controls_tab) {
                    controls_tab_active_=true;
                    if(edit_pending_ && !world_invalid_ && !weapon_invalid_) {
                        command.save=true; edit_pending_=false;
                    }
                    ImGui::BeginDisabled(binding_capture_.active());
                    auto sensitivity=sensitivity_edit_.value();
                    auto sensitivity_row=[&](const char *label,float &value,SensitivityControl control) {
                        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(label);
                        if(ImGui::GetContentRegionAvail().x>=400*scale) ImGui::SameLine(200*scale);
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x-ImGui::GetFrameHeight()-ImGui::GetStyle().ItemSpacing.x);
                        ImGui::PushID(label);
                        const auto before=value;
                        const bool multiplier=control==SensitivityControl::ads;
                        auto displayed=multiplier ? value : input::mouse_fps_units(value);
                        if(ImGui::SliderFloat("##sensitivity",&displayed,
                            multiplier ? input::aim_multiplier_min : input::mouse_fps_min,
                            multiplier ? input::aim_multiplier_max : input::mouse_fps_max,multiplier ? "%.2fx" : "%.2f",
                            ImGuiSliderFlags_AlwaysClamp)) {
                            value=multiplier ? displayed : input::mouse_degrees_per_count(displayed);
                            if(!sensitivity_edit_.edit(sensitivity)) value=before;
                        }
                        rectangle(sensitivity_rects_[static_cast<size_t>(control)]);
                        if(ImGui::IsItemHovered()) ImGui::SetTooltip(multiplier ?
                            "0.00x = no mouse aim movement. 1.00x = original weapon and zoom sensitivity. 2.00x = double. Ctrl+click to type a value." :
                            "1.00 = 0.022 degrees per mouse count. Also controls aiming speed. Ctrl+click to type a value.");
                        if(ImGui::IsItemDeactivatedAfterEdit()) sensitivity_edit_.finish();
                        ImGui::SameLine();
                        if(reset_button("##reset_sensitivity","Reset to default",scale)) {
                            value=multiplier ? input::MouseSensitivity{}.aim_multiplier : input::MouseSensitivity{}.normal;
                            sensitivity_edit_.edit(sensitivity); sensitivity_edit_.finish();
                        }
                        rectangle(sensitivity_rects_[static_cast<size_t>(multiplier ? SensitivityControl::ads_reset : SensitivityControl::normal_reset)]);
                        ImGui::PopID();
                    };
                    sensitivity_row("Mouse sensitivity",sensitivity.normal,SensitivityControl::normal);
                    sensitivity_row("Aim multiplier",sensitivity.aim_multiplier,SensitivityControl::ads);
                    ImGui::EndDisabled();
                    ImGui::Separator();
                    if(binding_capture_.active()) {
                        ImGui::TextWrapped("%s: %s",input::action_label(*binding_capture_.action()),
                            binding_capture_.armed() ? "Press a key or mouse button. Esc cancels." :
                                                       "Release held inputs.");
                        if(ImGui::Button("Cancel")) binding_capture_.cancel();
                        rectangle(binding_cancel_rect_);
                        if(!binding_capture_.error().empty())
                            ImGui::TextWrapped("%s",binding_capture_.error().c_str());
                    } else {
                        binding_cancel_rect_={};
                        ImGui::TextUnformatted("Click a binding to change it.");
                        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Replaces the inputs for that action. Mouse wheel is supported.\nInsert opens Settings; Escape closes. System shortcuts are reserved.");
                    }
                    if(!frame.controls_error.empty()) {
                        ImGui::TextWrapped("Controls are active but could not be saved: %s",frame.controls_error.c_str());
                        ImGui::BeginDisabled(binding_capture_.active());
                        if(ImGui::Button("Retry save")) command.retry_bindings_save=true;
                        ImGui::EndDisabled();
                    }
                    if(ImGui::BeginChild("Binding list",{0,-ImGui::GetFrameHeightWithSpacing()},
                                         ImGuiChildFlags_None)) {
                        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(10,4));
                        const bool table=ImGui::BeginTable("Bindings",2,ImGuiTableFlags_SizingStretchProp);
                        if(table) {
                        ImGui::TableSetupColumn("Action",ImGuiTableColumnFlags_WidthStretch,1.f);
                        ImGui::TableSetupColumn("Binding",ImGuiTableColumnFlags_WidthStretch,1.25f);
                        for(size_t i=0;i<input::action_count;++i) {
                            const auto action=static_cast<input::Action>(i);
                            ImGui::PushID(static_cast<int>(i));
                            ImGui::TableNextRow(); ImGui::TableNextColumn();
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextWrapped("%s",input::action_label(action));
                            if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",input::action_contexts(action));
                            ImGui::TableNextColumn();
                            const auto label=input::action_bindings(bindings_,action);
                            ImGui::BeginDisabled(binding_capture_.active());
                            if(ImGui::Button(label.c_str(),{-FLT_MIN,0})) {
                                binding_capture_.begin(action);
                                release_backend_input_locked();
                            }
                            rectangle(binding_rects_[i]);
                            ImGui::EndDisabled();
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                        }
                        ImGui::PopStyleVar();
                    }
                    ImGui::EndChild();
                    ImGui::BeginDisabled(binding_capture_.active());
                    if(ImGui::Button("Reset bindings")) {
                        input::reset_bindings(bindings_);
                        command.bindings=bindings_;
                    }
                    rectangle(controls_reset_rect_);
                    ImGui::EndDisabled();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            // Optical acknowledgement remains internal; the compact UI exposes
            // only its two requested values and persistence failures.
            const bool acknowledged=options_==frame.optical.requested &&
                                     options_==frame.optical.committed;
            command.preview_applied=acknowledged && frame.optical.application==camera::NativeWorldFov::Application::applied;
            command.preview_pending=!acknowledged || frame.optical.application==camera::NativeWorldFov::Application::deferred;
            if(!frame.last_save.error.empty()) ImGui::TextWrapped("Could not save: %s",frame.last_save.error.c_str());
        }
        if(frame.render_confirming) ImGui::OpenPopup("Keep this resolution?");
        if(ImGui::BeginPopupModal("Keep this resolution?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            if(!frame.render_confirming) ImGui::CloseCurrentPopup();
            else {
                ImGui::Text("Reverting in %d seconds.",frame.render_seconds_remaining);
                ImGui::BeginDisabled(frame.render_busy);
                if(ImGui::Button("Keep")) command.keep_render=true;
                rectangle(display_controls_[static_cast<size_t>(DisplayControl::keep)]);
                ImGui::SameLine();
                if(ImGui::Button("Revert")) command.revert_render=true;
                rectangle(display_controls_[static_cast<size_t>(DisplayControl::revert)]);
                ImGui::EndDisabled();
            }
            ImGui::EndPopup();
        }
        ImGui::End();
        if(movement_edit_pending_ && (!movement_gameplay_tab_active_ || !stay_open)) {
            command.movement_wobble=movement_wobble::Strength::from_percent(movement_wobble_seed_);
            movement_edit_pending_=false;
        }
        if(options_!=previous) { ++visible_revision_; command.changed=true; }
        command.closed=!stay_open;
        if(!stay_open) {
            if(edit_pending_ && !world_invalid_ && !weapon_invalid_) command.save=true;
            edit_pending_=false; visible_=false; sensitivity_edit_.finish(); binding_capture_.reset(); ImGui::ClearActiveID();
        } else if(command.save && !reset && (active_edit || edit_pending_)) {
            // Completing one row may activate/change the other in this frame.
            // Defer the single atomic pair until that new edit also completes.
            save_pending_=true; command.save=false;
        }
    }
    if(auto sensitivity=sensitivity_edit_.take()) command.sensitivity=sensitivity;
    ImGui::Render();
    command.wants_text=open && !command.closed && io.WantTextInput;
    command.visible=options_; command.visible_revision=visible_revision_;
    published_serial_=++ui_serial_; drawable_=!command.closed;
    return command; // Caller enqueues/saves/releases after this lock has ended.
}
void FovPanel::gl_presentable(bool enabled) {
    require_gl(); std::lock_guard lock(mutex_);
    if(!enabled || presentable_!=enabled) { drawable_=false; ++ui_serial_; }
    presentable_=enabled;
}
void FovPanel::gl_recreate_resources() {
    require_gl(); std::lock_guard lock(mutex_); select();
    if(!renderer_initialized_) return;
    GlState state; ImGui_ImplOpenGL3_DestroyDeviceObjects();
    ImGui_ImplOpenGL3_NewFrame(); // Exercise the guarded lazy creation entry.
    drawable_=false; ++ui_serial_; // New draw commands must use the new font ID.
}
FovUiRect FovPanel::control_rect(FovControl control) {
    require_ui(); std::lock_guard lock(mutex_);
    return controls_.at(static_cast<size_t>(control));
}
FovUiRect FovPanel::display_control_rect(DisplayControl control) {
    require_ui(); std::lock_guard lock(mutex_);
    return display_controls_.at(static_cast<size_t>(control));
}
FovUiRect FovPanel::binding_control_rect(input::Action action) {
    require_ui(); std::lock_guard lock(mutex_); return binding_rects_.at(static_cast<size_t>(action));
}
FovUiRect FovPanel::controls_tab_rect() {
    require_ui(); std::lock_guard lock(mutex_); return controls_tab_rect_;
}
FovUiRect FovPanel::controls_reset_rect() {
    require_ui(); std::lock_guard lock(mutex_); return controls_reset_rect_;
}
FovUiRect FovPanel::controls_cancel_rect() {
    require_ui(); std::lock_guard lock(mutex_); return binding_cancel_rect_;
}
FovUiRect FovPanel::sensitivity_control_rect(SensitivityControl control) {
    require_ui(); std::lock_guard lock(mutex_);
    return sensitivity_rects_[static_cast<size_t>(control)];
}
std::array<float,2> FovPanel::displayed_values() {
    require_ui(); std::lock_guard lock(mutex_);
    return {world_seed_,weapon_seed_};
}
void dispatch_fov_commands(camera::NativeWorldFov &runtime,const FovCommands &command,
                          const std::filesystem::path &path,FovSaveResult &last_save,
                          std::string &preview_error,const FovConfigIo *injected) {
    if(command.changed) preview_error=runtime.request_options(command.visible) ?
        "" : "Preview unavailable. Try another value.";
    if(command.save) {
        runtime.trace_completed_edit(command.visible);
        last_save=save_fov_config(path,command.visible,command.visible_revision,injected);
    }
}
void FovPanel::gl_draw(int width,int height) {
    require_gl(); std::lock_guard lock(mutex_); select();
    if(!renderer_ready_ || !drawable_ || width<=0 || height<=0) return;
    GlState state;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
    // This GL-only entry may recreate shaders/fonts lazily; guard covers it.
    ImGui_ImplOpenGL3_NewFrame();
    const auto *data=ImGui::GetDrawData();
    if(data && data->Valid && published_serial_!=0) {
        ImGui_ImplOpenGL3_RenderDrawData(const_cast<ImDrawData *>(data));
        drawn_serial_=published_serial_;
    }
}
} // namespace nova::platform

// Pinned SDL backend bridges. No desktop effect escapes the UI owner adapter.
// Every symbol matches the forced wrapper's decltype(SDL_...) declaration.
using nova::platform::PanelBackendEffects;
extern "C" {
bool SDLCALL nova_imgui_SDL_CaptureMouse(bool enabled) { PanelBackendEffects::current().capture=enabled; return true; }
bool SDLCALL nova_imgui_SDL_SetCursor(SDL_Cursor *cursor) { PanelBackendEffects::current().cursor=cursor; return true; }
bool SDLCALL nova_imgui_SDL_ShowCursor() { PanelBackendEffects::current().cursor_visible=true; return true; }
bool SDLCALL nova_imgui_SDL_HideCursor() { PanelBackendEffects::current().cursor_visible=false; return true; }
SDL_Cursor *SDLCALL nova_imgui_SDL_CreateSystemCursor(SDL_SystemCursor id) { return PanelBackendEffects::current().api.create_cursor(id); }
void SDLCALL nova_imgui_SDL_DestroyCursor(SDL_Cursor *cursor) { PanelBackendEffects::current().api.destroy_cursor(cursor); }
bool SDLCALL nova_imgui_SDL_StartTextInput(SDL_Window *window) { auto &e=PanelBackendEffects::current(); e.wants_text=true; e.ime_window=window; return true; }
bool SDLCALL nova_imgui_SDL_StopTextInput(SDL_Window *) { PanelBackendEffects::current().wants_text=false; return true; }
bool SDLCALL nova_imgui_SDL_SetTextInputArea(SDL_Window *,const SDL_Rect *area,int cursor) { auto &e=PanelBackendEffects::current(); e.area=*area; e.area_cursor=cursor; return true; }
// The host arbiter owns focus-click/capture policy. Do not change global hints.
bool SDLCALL nova_imgui_SDL_SetHint(const char *,const char *) { (void)PanelBackendEffects::current(); return true; }
// Keyboard navigation never warps the physical cursor. Event records supply it.
void SDLCALL nova_imgui_SDL_WarpMouseInWindow(SDL_Window *,float,float) { (void)PanelBackendEffects::current(); }
SDL_MouseButtonFlags SDLCALL nova_imgui_SDL_GetGlobalMouseState(float *x,float *y) { return PanelBackendEffects::current().api.global_mouse(x,y); }
SDL_Window *SDLCALL nova_imgui_SDL_GetKeyboardFocus() { return PanelBackendEffects::current().api.keyboard_focus(); }
bool SDLCALL nova_imgui_SDL_GetWindowPosition(SDL_Window *w,int *x,int *y) { return PanelBackendEffects::current().api.window_position(w,x,y); }
bool SDLCALL nova_imgui_SDL_GetWindowRelativeMouseMode(SDL_Window *w) { return PanelBackendEffects::current().api.relative_state(w); }
char *SDLCALL nova_imgui_SDL_GetClipboardText() { return PanelBackendEffects::current().api.get_clipboard(); }
bool SDLCALL nova_imgui_SDL_SetClipboardText(const char *text) { return PanelBackendEffects::current().api.set_clipboard(text); }
bool SDLCALL nova_imgui_SDL_OpenURL(const char *) { (void)PanelBackendEffects::current(); return false; }
}
