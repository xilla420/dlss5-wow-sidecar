#include "present/Hud.h"

#include <cstdio>

namespace sidecar {
namespace {

constexpr wchar_t kClassName[] = L"SidecarHud";
Hud* g_activeHud = nullptr;

const char* VerdictWord(GateVerdict v) {
  switch (v) {
    case GateVerdict::Playable: return "playable";
    case GateVerdict::Marginal: return "marginal";
    default:                    return "too slow";
  }
}

LRESULT CALLBACK HudProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

}  // namespace

GateVerdict JudgeGate(double p99Ms) {
  if (p99Ms < 40.0) return GateVerdict::Playable;
  if (p99Ms < 80.0) return GateVerdict::Marginal;
  return GateVerdict::Failed;
}

std::string FormatHud(const HudModel& model) {
  char buffer[512];
  std::snprintf(buffer, sizeof(buffer),
                "%s | %s | p50 %.1f ms  p99 %.1f ms (%s) | %llu frames  %llu drops",
                model.gpuName, model.passName, model.p50Ms, model.p99Ms,
                VerdictWord(JudgeGate(model.p99Ms)),
                static_cast<unsigned long long>(model.frames),
                static_cast<unsigned long long>(model.drops));
  return buffer;
}

namespace {

LRESULT CALLBACK HudProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_PAINT && g_activeHud) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client{};
    GetClientRect(hwnd, &client);
    FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0x43, 0xBA, 0xB4));
    HFONT font = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             FIXED_PITCH | FF_MODERN, L"Consolas");
    HGDIOBJ previous = SelectObject(dc, font);
    // Text is ASCII-only by construction in FormatHud.
    TextOutA(dc, 8, 6, g_activeHud->TextForPaint().c_str(),
             static_cast<int>(g_activeHud->TextForPaint().size()));
    SelectObject(dc, previous);
    DeleteObject(font);
    EndPaint(hwnd, &ps);
    return 0;
  }
  if (msg == WM_NCHITTEST) return HTTRANSPARENT;
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

std::unique_ptr<Hud> Hud::Create() {
  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = HudProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    registered = true;
  }

  std::unique_ptr<Hud> h(new Hud());
  h->hwnd_ = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOPMOST |
          WS_EX_TOOLWINDOW,
      // Wide enough that the drop count is not clipped: the operator reads
      // p50, p99 and drops together to make the M1 gate call.
      kClassName, L"", WS_POPUP, 12, 12, 900, 28,
      nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!h->hwnd_) return nullptr;
  SetLayeredWindowAttributes(h->hwnd_, 0, 210, LWA_ALPHA);
  g_activeHud = h.get();
  return h;
}

Hud::~Hud() {
  if (g_activeHud == this) g_activeHud = nullptr;
  if (hwnd_) DestroyWindow(hwnd_);
}

void Hud::Update(const HudModel& model) {
  text_ = FormatHud(model);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Hud::Show() { ShowWindow(hwnd_, SW_SHOWNOACTIVATE); }
void Hud::Hide() noexcept { if (hwnd_) ShowWindow(hwnd_, SW_HIDE); }

}  // namespace sidecar
