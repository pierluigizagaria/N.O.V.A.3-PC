#include "net/social_invitations.h"
#include "net/pc_snapshot.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <map>
#include <mutex>
#include <thread>
#include <windows.h>

namespace nova::net::social {
namespace {
using Clock = std::chrono::steady_clock;
struct Invitation {
  std::string event, id, name;
  uint32_t room;
  Clock::time_point expires;
};
std::mutex guard;
enum class Status { queued, showing, waiting, delivered, declined };
struct Record {
  Invitation invitation;
  Status status = Status::queued;
  bool consented = false;
};
std::deque<std::string> pending;
std::map<std::string, Record> records;
std::string accepted;
uint64_t generation = 0;
struct Prompt {
  Invitation invitation;
  uint64_t epoch;
  std::wstring text;
  std::atomic<HWND> window{nullptr};
  std::atomic<bool> cancelled{false}, done{false};
  int choice = IDNO; // Published by done's release/acquire boundary.
};
std::shared_ptr<Prompt> prompt;
thread_local Prompt *opening_prompt = nullptr;

LRESULT CALLBACK track_prompt(int code, WPARAM window, LPARAM data) {
  if (code == HCBT_ACTIVATE && opening_prompt) {
    const auto hwnd = reinterpret_cast<HWND>(window);
    opening_prompt->window.store(hwnd, std::memory_order_release);
    if (opening_prompt->cancelled.load(std::memory_order_acquire))
      PostMessageW(hwnd, WM_COMMAND, IDNO, 0);
  }
  return CallNextHookEx(nullptr, code, window, data);
}
void cancel_prompt(const std::shared_ptr<Prompt> &p) {
  if (!p)
    return;
  p->cancelled.store(true, std::memory_order_release);
  if (const auto window = p->window.load(std::memory_order_acquire))
    PostMessageW(window, WM_COMMAND, IDNO, 0);
}
void show_prompt(std::shared_ptr<Prompt> p) {
  // This worker owns only standard Windows UI and shared prompt storage.
  // It never enters the game/JNI or holds the invitation queue mutex.
  opening_prompt = p.get();
  const auto hook = SetWindowsHookExW(WH_CBT, track_prompt, nullptr, GetCurrentThreadId());
  if (hook && !p->cancelled.load(std::memory_order_acquire)) {
    // An owner on the game thread would disable that game window. Keep this
    // independent prompt visible while rendering, input and lobby keep running.
    p->choice = MessageBoxW(nullptr, p->text.c_str(), L"N.O.V.A. 3 invitation",
        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_SETFOREGROUND | MB_TOPMOST);
  }
  if (hook)
    UnhookWindowsHookEx(hook);
  opening_prompt = nullptr;
  p->window.store(nullptr, std::memory_order_release);
  p->done.store(true, std::memory_order_release);
}

bool label(std::span<const uint8_t> text) {
  return !text.empty() && text.size() <= 16 &&
         std::all_of(text.begin(), text.end(), [](uint8_t c) {
           return c >= 32 && c != 127;
         });
}
} // namespace

bool receive(std::span<const uint8_t> body) {
  if (body.size() < 12 || (body[0] != 1 && body[0] != 2) || body[1] != 1 ||
      body[8] < 1 || body[8] > 16 || body[9] < 1 || body[9] > 16 ||
      body.size() != size_t((body[0] == 2 ? 26 : 10) + body[8] + body[9]))
    return false;
  const auto room = pc::read32(body.data() + 2);
  const auto ttl = pc::read16(body.data() + 6);
  const size_t offset = body[0] == 2 ? 26 : 10;
  const auto id = body.subspan(offset, body[8]);
  const auto name = body.subspan(offset + body[8], body[9]);
  if ((body[0] == 1 && !room) || !ttl || ttl > 300 || !label(id) || !label(name))
    return false;
  std::string event;
  if (body[0] == 2) {
    const auto nonce = body.subspan(10, 16);
    if (std::all_of(nonce.begin(), nonce.end(), [](auto c) { return c == 0; }))
      return false;
    event.assign(nonce.begin(), nonce.end());
  } else {
    // Older servers lack event IDs. Bound their conservative duplicate window
    // to the invitation lifetime; new explicit invitations use fresh v2 IDs.
    event = "legacy:" + std::string(id.begin(), id.end()) + ":" + std::to_string(room);
  }
  Invitation invitation{event, std::string(id.begin(), id.end()),
                        std::string(name.begin(), name.end()), room,
                        Clock::now() + std::chrono::seconds(ttl)};
  std::lock_guard lock(guard);
  const auto now = Clock::now();
  std::erase_if(records, [&](const auto &entry) { return now >= entry.second.invitation.expires; });
  std::erase_if(pending, [&](const auto &key) { return !records.contains(key); });
  if (const auto found = records.find(event); found != records.end()) {
    auto &v = found->second;
    if (v.invitation.id != invitation.id || v.invitation.name != invitation.name)
      return false;
    v.invitation.expires = std::min(v.invitation.expires, invitation.expires);
    if (room)
      v.invitation.room = room; // A stale room0 replay cannot undo the binding.
    if (v.status == Status::delivered || v.status == Status::declined || v.status == Status::showing)
      return true;
    if (v.status == Status::waiting && !v.invitation.room)
      return true;
    if (pending.size() < 8 && std::find(pending.begin(), pending.end(), event) == pending.end()) {
      v.status = Status::queued;
      pending.push_back(event);
    }
    return true;
  }
  if (records.size() < 128 && pending.size() < 8) {
    records.emplace(event, Record{std::move(invitation)});
    pending.push_back(event);
  }
  return true;
}

bool poll_invitation() {
  std::shared_ptr<Prompt> next;
  {
    std::lock_guard lock(guard);
    if (!accepted.empty())
      return true;
    if (prompt) {
      const auto found = records.find(prompt->invitation.event);
      if (prompt->cancelled.load(std::memory_order_acquire) ||
          found == records.end() || Clock::now() >= found->second.invitation.expires)
        cancel_prompt(prompt);
      if (!prompt->done.load(std::memory_order_acquire))
        return false;
      if (prompt->choice == IDYES && prompt->epoch == generation &&
          !prompt->cancelled.load(std::memory_order_acquire) &&
          found != records.end() && found->second.status == Status::showing &&
          Clock::now() < found->second.invitation.expires) {
        auto &v = found->second;
        v.consented = true;
        v.status = v.invitation.room ? Status::delivered : Status::waiting;
        if (v.invitation.room)
          accepted = v.invitation.id;
      } else if (found != records.end() && found->second.status == Status::showing) {
        found->second.status = Status::declined;
      }
      prompt.reset();
      if (!accepted.empty())
        return true;
    }
    while (!pending.empty()) {
      const auto event = std::move(pending.front());
      pending.pop_front();
      const auto found = records.find(event);
      if (found == records.end() || Clock::now() >= found->second.invitation.expires ||
          found->second.status != Status::queued)
        continue;
      auto &v = found->second;
      if (v.consented) {
        v.status = v.invitation.room ? Status::delivered : Status::waiting;
        if (v.invitation.room) {
          accepted = v.invitation.id;
          return true;
        }
        continue;
      }
      next = std::make_shared<Prompt>();
      next->invitation = v.invitation;
      next->epoch = generation;
      v.status = Status::showing;
      prompt = next;
      break;
    }
    if (!next)
      return false;
  }
  // Android delivered this intent after the player tapped its notification.
  // Obtain the same explicit choice on PC, then let the original menu perform
  // its normal autoStartGame(2), location108 and friend-room join sequence.
  wchar_t name[33]{};
  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
      next->invitation.name.data(), static_cast<int>(next->invitation.name.size()), name, 32);
  next->text = (length ? std::wstring(name, length) : L"A player") +
      (next->invitation.room ? L" invited you to join a game.\n\nJoin this game?" :
       L" invited you to play online.\n\nJoin when their room is ready?");
  try {
    std::thread(show_prompt, next).detach();
  } catch (...) {
    next->done.store(true, std::memory_order_release);
  }
  return false;
}

std::string inviter() {
  std::lock_guard lock(guard);
  return accepted;
}
void reset_notification() {
  std::lock_guard lock(guard);
  accepted.clear();
}
void clear() {
  std::lock_guard lock(guard);
  pending.clear();
  accepted.clear();
  ++generation;
  cancel_prompt(prompt);
  // Keep event receipts through lobby reconnects, including the original
  // invitation-accept transition. IDs are random and scoped to one recipient;
  // another account's invitation cannot reuse its consent or receipt.
  for (auto &[event, record] : records) {
    (void)event;
    if (record.status == Status::showing)
      record.status = Status::declined;
  }
}
} // namespace nova::net::social
