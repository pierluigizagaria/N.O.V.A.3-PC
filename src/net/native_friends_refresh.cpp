#include "net/native_friends_refresh.h"
#include "abi/guest_hook.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <windows.h>

namespace nova::net {
namespace {
using Clock = std::chrono::steady_clock;
uint32_t word(uint32_t address) {
  uint32_t value;
  std::memcpy(&value, reinterpret_cast<const void *>(address), sizeof(value));
  return value;
}
[[noreturn]] void failed() {
  std::fputs("Native friends refresh boundary failed\n", stderr);
  TerminateProcess(GetCurrentProcess(), 86);
  std::abort();
}
} // namespace

struct NativeFriendsRefresh::Impl {
  static Impl *active;
  std::unique_ptr<abi::GuestHook> update, general_update, parsed;
  uint32_t manager, request, redraw, get_selected, set_selected;
  uint32_t menus, find_object, slide_child, get_state, application, get_string, set_text;
  std::atomic<uint32_t> revision{0};
  uint32_t shown_revision = 0, requested_revision = 0, last_screen = 0;
  bool waiting = false;
  Clock::time_point last_update{}, next_request{}, request_deadline{};

  explicit Impl(const ElfImage &image) {
    if (active)
      throw std::runtime_error("Native friends refresh already configured");
    manager = image.symbol("_ZN14XPlayerManager9SingletonE");
    if (!manager)
      throw std::runtime_error("Native friends manager is missing");
    request = abi::require_function(image, "_ZN19GLXPlayerUserFriend18sendGetUserFriendsEiib");
    redraw = abi::require_function(image, "_ZN20GS_OnlineFriendsList17UpdateFriendsListEv");
    get_selected = abi::require_function(image, "_ZN10CMenuTable11GetSelectedEv");
    set_selected = abi::require_function(image, "_ZN10CMenuTable11SetSelectedEi");
    menus = image.symbol("_ZN12CMenuManager9SingletonE");
    if (!menus)
      throw std::runtime_error("Native friends menu manager is missing");
    find_object = abi::require_function(image, "_ZN12CMenuManager10FindObjectEi");
    slide_child = abi::require_function(image, "_ZNK14CMenuSlideShow27GetSlideChildFromTemplateIdEii");
    get_state = abi::require_function(image, "_ZN19GLXPlayerUserFriend8GetStateEi");
    application = abi::require_function(image, "_ZN11Application11GetInstanceEv");
    get_string = abi::require_function(image, "_ZNK11Application9GetStringEi");
    set_text = abi::require_function(image, "_ZN9CMenuText13SetCustomTextEPKc");
    // Stop before each PIC thunk: only these verified nonrelative instructions
    // are copied into the existing aligned GuestHook trampoline.
    update = std::make_unique<abi::GuestHook>(image,
        "_ZN20GS_OnlineFriendsList6UpdateEv",
        std::initializer_list<uint8_t>{0x55, 0x89, 0xe5, 0x57, 0x56, 0x53},
        reinterpret_cast<uint32_t>(&tick));
    general_update = std::make_unique<abi::GuestHook>(image,
        "_ZN14GS_FriendsList6UpdateEv",
        std::initializer_list<uint8_t>{0x55, 0x89, 0xe5, 0x57, 0x56, 0x53},
        reinterpret_cast<uint32_t>(&general_tick));
    parsed = std::make_unique<abi::GuestHook>(image,
        "_ZN19GLXPlayerUserFriend18processFriendsListEPc",
        std::initializer_list<uint8_t>{0x55, 0x89, 0xe5, 0x57, 0x56, 0x53},
        reinterpret_cast<uint32_t>(&received));
    active = this;
  }
  ~Impl() {
    parsed.reset();
    update.reset();
    general_update.reset();
    active = nullptr;
  }

  static uint32_t __cdecl received(uint32_t component, uint32_t body) noexcept {
    try {
      auto &self = *active;
      const auto result = abi::invoke(self.parsed->original(), {component, body});
      self.revision.fetch_add(1, std::memory_order_release);
      return result;
    } catch (...) { failed(); }
  }
  static uint32_t __cdecl tick(uint32_t screen) noexcept {
    return tick_screen(screen, true);
  }
  static uint32_t __cdecl general_tick(uint32_t screen) noexcept {
    return tick_screen(screen, false);
  }
  bool redraw_general(uint32_t screen, uint32_t friends) {
    // GS_FriendsList builds its slideshow and status labels once, guarded by
    // byte+0x30. Refresh only those labels, preserving its avatar requests,
    // slide selection and native layout. These IDs are the original UI bindings.
    if (!*reinterpret_cast<const uint8_t *>(screen + 0x30))
      return false;
    const auto menu = word(menus);
    const auto slides = menu ? abi::invoke(find_object, {menu, 0x3bd80}) : 0;
    if (!slides)
      return false;
    const auto app = abi::invoke(application);
    const auto count = std::min(word(screen + 0x3c), word(friends + 0x40));
    for (uint32_t i = 0; i < count; ++i) {
      const auto node = abi::invoke(slide_child, {slides, i, 0x3bd8b});
      if (!node)
        continue;
      const auto state = abi::invoke(get_state, {friends, i});
      const uint32_t label = state == 2 ? 7 : state == 3 ? 9 : state == 4 ? 10 : 8;
      const auto text = abi::invoke(get_string, {app, label});
      abi::invoke(set_text, {node, text});
    }
    return true;
  }
  static uint32_t tick_screen(uint32_t screen, bool online_table) noexcept {
    try {
      auto &self = *active;
      const auto now = Clock::now();
      const bool entered = self.last_screen != screen ||
          now - self.last_update > std::chrono::seconds(1);
      self.last_screen = screen;
      self.last_update = now;
      const auto manager = word(self.manager);
      const auto live = manager ? word(manager + 8) : 0;
      const auto friends = live ? word(live + 0xc) : 0;
      const auto table = online_table ? word(screen + 0x34) : 0;
      const auto revision = self.revision.load(std::memory_order_acquire);
      if (friends && table && (entered || revision != self.shown_revision)) {
        // Preserve the current selection instead of returning the cursor to
        // the first friend whenever a fresh presence response arrives.
        const auto selection = static_cast<int32_t>(abi::invoke(self.get_selected, {table}));
        abi::invoke(self.redraw, {screen});
        const auto count = word(friends + 0x40);
        if (selection >= 0 && static_cast<uint32_t>(selection) < count)
          abi::invoke(self.set_selected, {table, static_cast<uint32_t>(selection)});
        self.shown_revision = revision;
      } else if (friends && !online_table &&
                 (entered || revision != self.shown_revision) &&
                 self.redraw_general(screen, friends)) {
        self.shown_revision = revision;
      }
      if (self.waiting && revision != self.requested_revision)
        self.waiting = false;
      if (friends && word(friends + 8) && now >= self.next_request &&
          (!self.waiting || now >= self.request_deadline)) {
        self.requested_revision = revision;
        self.waiting = true;
        self.next_request = now + std::chrono::seconds(5);
        self.request_deadline = now + std::chrono::seconds(15);
        // Reuse native61 authentication, framing, parsing and error handling.
        // No refresh requests are generated while this screen is closed.
        abi::invoke(self.request, {friends, 0, 0, 1});
      }
      // The original update can pop/destroy this screen. Do not read it after.
      // The general friends screen has no CMenuTable at+0x34.
      return abi::invoke(online_table ? self.update->original() : self.general_update->original(), {screen});
    } catch (...) { failed(); }
  }
};
NativeFriendsRefresh::Impl *NativeFriendsRefresh::Impl::active = nullptr;
NativeFriendsRefresh::NativeFriendsRefresh(const ElfImage &image)
    : impl_(std::make_unique<Impl>(image)) {}
NativeFriendsRefresh::~NativeFriendsRefresh() = default;
} // namespace nova::net
