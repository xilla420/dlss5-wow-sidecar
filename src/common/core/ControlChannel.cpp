#include "core/ControlChannel.h"

#include <cstring>

namespace sidecar {
namespace {

constexpr wchar_t kClassName[] = L"DLSS5SidecarControl";
constexpr wchar_t kMappingName[] = L"Local\\DLSS5SidecarStatus";
constexpr UINT kCommandMessage = WM_APP + 1;

// Message-only windows are not enumerated by the usual desktop searches, which
// is the point: this window exists to be found by one process by name, and by
// nothing else at all.
HWND FindControlWindow() {
  return FindWindowExW(HWND_MESSAGE, nullptr, kClassName, nullptr);
}

}  // namespace

LRESULT CALLBACK ControlServer::Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == kCommandMessage) {
    auto* self = reinterpret_cast<ControlServer*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self && self->handler_) self->handler_(static_cast<SidecarCommand>(wp));
    return 1;   // "received", which is what the manager waits for
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

std::unique_ptr<ControlServer> ControlServer::Create(Handler handler) {
  // One overlay at a time. Two would each cover the screen with a stale copy of
  // the other's output, and the second one to start would look like a hang.
  if (FindControlWindow()) return nullptr;

  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = &ControlServer::Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return nullptr;
    registered = true;
  }

  std::unique_ptr<ControlServer> server(new ControlServer());
  server->handler_ = std::move(handler);
  server->hwnd_ = CreateWindowExW(0, kClassName, L"", 0, 0, 0, 0, 0,
                                  HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr),
                                  nullptr);
  if (!server->hwnd_) return nullptr;
  SetWindowLongPtrW(server->hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(server.get()));

  // A failed mapping costs the manager its live numbers and nothing else, so it
  // is not allowed to fail the channel: the commands are the part that matters.
  server->mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                        sizeof(SidecarStatus), kMappingName);
  if (server->mapping_) {
    server->shared_ = static_cast<SidecarStatus*>(
        MapViewOfFile(server->mapping_, FILE_MAP_WRITE, 0, 0, sizeof(SidecarStatus)));
  }
  if (server->shared_) {
    *server->shared_ = SidecarStatus{};
    server->shared_->processId = GetCurrentProcessId();
  }
  return server;
}

ControlServer::~ControlServer() {
  if (shared_) UnmapViewOfFile(shared_);
  if (mapping_) CloseHandle(mapping_);
  if (hwnd_) DestroyWindow(hwnd_);
}

void ControlServer::Publish(const SidecarStatus& status) {
  if (!shared_) return;

  // Seqlock: the reader retries while the counter is odd or moved under it. The
  // manager polls this several times a second and the write is a few dozen
  // bytes, so a torn read is unlikely -- but "unlikely" in a struct holding a
  // string is a garbled diagnostic, which is worse than a dropped frame of UI.
  const uint32_t next = shared_->sequence + 1;
  shared_->sequence = next;                          // now odd: write in progress
  MemoryBarrier();

  SidecarStatus copy = status;
  copy.sequence = next;
  copy.processId = GetCurrentProcessId();
  std::memcpy(shared_, &copy, sizeof(copy));

  MemoryBarrier();
  shared_->sequence = next + 1;                      // even again: readable
}

namespace control {

bool IsRunning() { return FindControlWindow() != nullptr; }

bool Send(SidecarCommand command) {
  HWND hwnd = FindControlWindow();
  if (!hwnd) return false;
  DWORD_PTR result = 0;
  // A frozen overlay must not freeze the manager with it. Two seconds is long
  // enough for a busy render thread and short enough to notice.
  const LRESULT sent = SendMessageTimeoutW(hwnd, kCommandMessage,
                                           static_cast<WPARAM>(command), 0,
                                           SMTO_ABORTIFHUNG, 2000, &result);
  return sent != 0 && result != 0;
}

std::optional<SidecarStatus> Read() {
  HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, kMappingName);
  if (!mapping) return std::nullopt;
  auto* view = static_cast<const SidecarStatus*>(
      MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(SidecarStatus)));
  if (!view) {
    CloseHandle(mapping);
    return std::nullopt;
  }

  std::optional<SidecarStatus> out;
  for (int attempt = 0; attempt < 4; ++attempt) {
    const uint32_t before = view->sequence;
    if (before % 2 != 0) continue;                   // a write is in progress
    SidecarStatus copy;
    std::memcpy(&copy, view, sizeof(copy));
    MemoryBarrier();
    if (view->sequence != before) continue;          // it moved under us
    // Never trust a string from shared memory to be terminated.
    copy.passName[sizeof(copy.passName) - 1] = '\0';
    copy.runtimeVariant[sizeof(copy.runtimeVariant) - 1] = '\0';
    copy.lastError[sizeof(copy.lastError) - 1] = '\0';
    out = copy;
    break;
  }

  UnmapViewOfFile(view);
  CloseHandle(mapping);
  return out;
}

}  // namespace control
}  // namespace sidecar
