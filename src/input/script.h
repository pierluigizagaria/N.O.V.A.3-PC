#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <istream>
#include <string>
#include <vector>

namespace nova::input {
class Script {
  public:
    static Script parse(std::istream &stream);
    bool scheduled(uint64_t frame) const;
    void push(uint64_t frame, SDL_WindowID window, double elapsed_ms);
    // Unwrap only our registered SDL user event; physical events cannot masquerade as scripts.
    static bool unwrap(SDL_Event &event, bool *native_only = nullptr);
    size_t size() const { return events_.size(); }
    uint64_t last_frame() const { return events_.empty() ? 0 : events_.back().frame; }

  private:
    struct Event {
        uint64_t frame;
        SDL_Event event;
        std::string text;
        size_t line;
        bool native_only;
    };
    std::vector<Event> events_;
    size_t next_ = 0;
};
} // namespace nova::input
