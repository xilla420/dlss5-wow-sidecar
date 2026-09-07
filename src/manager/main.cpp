// wowsidecar-manager.exe: the control panel.
//
// It does five things, and refuses to do a sixth. It reports what the machine
// has and what it is missing; it copies operator-supplied files into place; it
// starts and stops the overlay; it edits the settings both the sidecar and the
// RenoDX add-on read; and it shows the overlay's live numbers while it runs.
//
// What it does not do is touch a WoW installation, in either direction. The
// probes it runs are the same predicates the unit tests pin down (I7, I8, I9),
// and the uninstaller deletes only files inside the sidecar's own directory.
#include <windows.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <misc/cpp/imgui_stdlib.h>

#include "core/Config.h"
#include "core/ControlChannel.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "core/Utf8.h"
#include "core/Version.h"
#include "manager/Install.h"
#include "manager/Probes.h"
#include "manager/Theme.h"
#include "manager/WowInstall.h"
#include "neural/AddonSettings.h"
#include "neural/NgxSession.h"
#include "present/Hud.h"
#include "present/WindowTracker.h"

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;
using namespace sidecar;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

// The design sizes, at scale 1. Not constexpr any more: the interface is drawn
// at whatever the monitor's DPI and the operator's setting ask for, and these
// are multiplied once at startup rather than at every use, so the hundred or so
// places that read them need no arithmetic of their own.
constexpr int kWindowWidth = 1180;
constexpr int kWindowHeight = 820;
float kNavWidth = 208.0f;
float kHeaderHeight = 104.0f;
float kPad = 22.0f;

// How much bigger than the design size everything is drawn. Read by S() below,
// set once the window exists and the DPI can be asked for.
float g_scale = 1.0f;

// A length in design pixels, in the pixels it is actually drawn at.
inline float S(float designPixels) { return designPixels * g_scale; }

ComPtr<ID3D11Device> g_device;
ComPtr<ID3D11DeviceContext> g_context;
ComPtr<IDXGISwapChain1> g_swapChain;
ComPtr<ID3D11RenderTargetView> g_backBufferRtv;

// The notice a first-time operator has to read before anything is enabled.
// Wording is fixed by the spec; it is the honest version of the safety claim,
// not a disclaimer.
constexpr const char* kFirstRunTitle = "Before you use this";
// Translated where it is drawn rather than here, because a constexpr pointer
// cannot hold the result of a lookup.
constexpr const char* kFirstRunBody =
    "This sidecar never loads code into Wow.exe. That is what makes it safe, "
    "and it is checked automatically every time it is built.\n\n"
    "ReShade placed next to Wow.exe is a different thing entirely, and "
    "Blizzard bans accounts for it. This tool will refuse to install there and "
    "will refuse to run if it finds an injector in your WoW folder.\n\n"
    "No third-party tool can promise you will never be banned. What this one "
    "can promise is that it does not do any of the things Blizzard bans people "
    "for.";

// ReShade must not load into this process.
//
// Its dxgi.dll sits in this directory so the *runtime* picks it up, but the
// loader offers it to anything in the folder that touches DXGI -- and this
// manager does, for its own ImGui swapchain. The result is a second ReShade
// with a second copy of the add-on and a 160 MB neural runtime mapped into a
// control panel that has no use for any of it, plus a ReShade.log that
// documents the manager and reads exactly like the runtime's.
//
// So both DXGI and D3D11 are delay-loaded (see CMakeLists), and this runs
// first: once the system copies are loaded under those names, the loader will
// not go looking for a second module with the same base name, and ReShade's
// copy is never consulted. It must happen before the first D3D call.
void PinSystemGraphicsDlls() {
  LoadLibraryExW(L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
}

void CreateBackBufferRtv() {
  ComPtr<ID3D11Texture2D> back;
  if (SUCCEEDED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back)))) {
    g_device->CreateRenderTargetView(back.Get(), nullptr, &g_backBufferRtv);
  }
}

bool CreateDeviceAndSwapChain(HWND hwnd) {
  DXGI_SWAP_CHAIN_DESC1 scd{};
  scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  scd.SampleDesc.Count = 1;
  scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  scd.BufferCount = 2;
  scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                               _countof(levels), D3D11_SDK_VERSION,
                               &g_device, nullptr, &g_context))) {
    return false;
  }

  ComPtr<IDXGIDevice> dxgiDevice;
  ComPtr<IDXGIAdapter> adapter;
  ComPtr<IDXGIFactory2> factory;
  if (FAILED(g_device.As(&dxgiDevice)) ||
      FAILED(dxgiDevice->GetAdapter(&adapter)) ||
      FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
    return false;
  }
  if (FAILED(factory->CreateSwapChainForHwnd(g_device.Get(), hwnd, &scd, nullptr,
                                             nullptr, &g_swapChain))) {
    return false;
  }
  CreateBackBufferRtv();
  return true;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 1;
  switch (msg) {
    case WM_SIZE:
      if (g_swapChain && wp != SIZE_MINIMIZED) {
        g_backBufferRtv.Reset();
        g_swapChain->ResizeBuffers(0, LOWORD(lp), HIWORD(lp), DXGI_FORMAT_UNKNOWN, 0);
        CreateBackBufferRtv();
      }
      return 0;
    case WM_GETMINMAXINFO:
      // Below this the nav rail and the content start fighting each other.
      reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = POINT{900, 620};
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wp, lp);
  }
}

// The last lines of a log file, or nothing when it is not there.
//
// Reads from the end rather than the beginning: the overlay's log grows for as
// long as it runs, and the interesting part is always the newest. The cap is
// generous enough to hold a whole session's worth of a normal run and small
// enough that re-reading it costs nothing.
std::vector<std::string> TailOfFile(const fs::path& path, size_t maxLines) {
  std::error_code ec;
  const auto size = fs::file_size(path, ec);
  if (ec) return {};

  std::ifstream file(path, std::ios::binary);
  if (!file) return {};

  constexpr uintmax_t kMostToRead = 256 * 1024;
  if (size > kMostToRead) {
    file.seekg(static_cast<std::streamoff>(size - kMostToRead), std::ios::beg);
    // The seek almost certainly landed mid-line; drop the partial one rather
    // than show a sentence that starts in the middle of a word.
    std::string partial;
    std::getline(file, partial);
  }

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(line);
    // Kept bounded as it is read, so a log that grew past the cap between two
    // reads cannot make this allocate without limit.
    if (lines.size() > maxLines * 2) {
      lines.erase(lines.begin(), lines.begin() + static_cast<long>(maxLines));
    }
  }
  if (lines.size() > maxLines) {
    lines.erase(lines.begin(), lines.end() - static_cast<long>(maxLines));
  }
  return lines;
}

fs::path ExecutableDirectory() {
  wchar_t buffer[MAX_PATH]{};
  GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  return fs::path(buffer).parent_path();
}

ImVec4 Rgb(unsigned int hex, float alpha = 1.0f) {
  return ImVec4(((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f,
                (hex & 0xFF) / 255.0f, alpha);
}

const char* StateLabel(ProbeState state) {
  switch (state) {
    case ProbeState::Ok:   return Tr("READY");
    case ProbeState::Warn: return Tr("CHECK");
    default:               return Tr("BLOCKED");
  }
}

ImVec4 StateColor(ProbeState state, const ThemeColors& colors) {
  return Rgb(state == ProbeState::Ok ? colors.ok
             : state == ProbeState::Warn ? colors.warn
                                         : colors.fail);
}

ThemeFonts g_fonts;
ThemeColors g_colors;

// A bronze rule across the available width. The game separates everything with
// one of these, and it does more for the resemblance than any amount of colour.
// padBelow is a length and scales; alpha is not and does not.
void GoldRule(float alpha = 0.45f, float padBelow = 10.0f) {
  padBelow = S(padBelow);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 at = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;
  draw->AddLine(ImVec2(at.x, at.y), ImVec2(at.x + width, at.y),
                ImGui::GetColorU32(Rgb(g_colors.accent, alpha)), 1.0f);
  ImGui::Dummy(ImVec2(0.0f, padBelow));
}

void SectionHeading(const char* text) {
  if (g_fonts.heading) ImGui::PushFont(g_fonts.heading);
  ImGui::TextColored(Rgb(g_colors.goldBright), "%s", Tr(text));
  if (g_fonts.heading) ImGui::PopFont();
  GoldRule(0.35f, 8.0f);
}

// A short explanation under a control, in the muted colour and the small face.
void Hint(const char* text) {
  if (g_fonts.caption) ImGui::PushFont(g_fonts.caption);
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", Tr(text));
  ImGui::PopStyleColor();
  if (g_fonts.caption) ImGui::PopFont();
}

// A filled dot plus a label. Every WoW player reads a green dot without being
// told what it means, which is the whole reason status is coloured this way.
void Dot(bool good, const char* label, bool warnNotFail = true) {
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 at = ImGui::GetCursorScreenPos();
  const float radius = S(5.0f);
  const ImU32 colour = ImGui::GetColorU32(
      good ? Rgb(g_colors.ok) : Rgb(warnNotFail ? g_colors.warn : g_colors.fail));
  draw->AddCircleFilled(ImVec2(at.x + radius + S(1.0f), at.y + ImGui::GetTextLineHeight() * 0.5f),
                        radius, colour);
  ImGui::Dummy(ImVec2(radius * 2.0f + S(8.0f), 0.0f));
  ImGui::SameLine(0.0f, 0.0f);
  ImGui::TextUnformatted(Tr(label));
}

// One number, labelled, in a bordered slot. The game shows statistics like
// this and it beats a run of "label: value" lines for scanning.
void StatCard(const char* label, const char* value, float width, ImVec4 valueColor) {
  ImGui::BeginChild(label, ImVec2(width, S(74.0f)), ImGuiChildFlags_Border);
  ImGui::Dummy(ImVec2(0.0f, S(2.0f)));
  ImGui::Indent(S(12.0f));
  if (g_fonts.caption) ImGui::PushFont(g_fonts.caption);
  ImGui::TextDisabled("%s", Tr(label));
  if (g_fonts.caption) ImGui::PopFont();
  if (g_fonts.heading) ImGui::PushFont(g_fonts.heading);
  ImGui::TextColored(valueColor, "%s", value);
  if (g_fonts.heading) ImGui::PopFont();
  ImGui::Unindent(S(12.0f));
  ImGui::EndChild();
}

// The common shell dialog, which is the only reasonable way to let someone
// point at a file they downloaded ten minutes ago. Returns an empty path when
// they cancel, which is not an error.
fs::path AskForFile(HWND owner, const wchar_t* filter, const wchar_t* title) {
  wchar_t buffer[MAX_PATH]{};
  OPENFILENAMEW ofn{sizeof(ofn)};
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = filter;
  ofn.lpstrFile = buffer;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrTitle = title;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&ofn)) return {};
  return fs::path(buffer);
}

// The folder equivalent, for the one folder this program wants to know about.
// It is a separate function because the folder picker is not the file picker
// with a flag: GetOpenFileNameW cannot select a directory at all, which is how
// the WoW folder ended up as a field you had to type a path into.
//
// COM is initialised here rather than in wWinMain because this is the only
// thing in the manager that needs it, and it is uninitialised again only when
// this call is the one that started it -- decrementing somebody else's
// reference would tear COM down underneath them.
fs::path AskForFolder(HWND owner, const wchar_t* title, const fs::path& startAt) {
  const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  const bool weStartedIt = SUCCEEDED(init);

  fs::path chosen;
  IFileDialog* dialog = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&dialog)))) {
    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
      dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }
    dialog->SetTitle(title);

    // Open where the current guess points, so confirming a detected folder is
    // one click rather than a walk down the drive.
    if (!startAt.empty()) {
      IShellItem* item = nullptr;
      if (SUCCEEDED(SHCreateItemFromParsingName(startAt.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
        dialog->SetFolder(item);
        item->Release();
      }
    }

    if (SUCCEEDED(dialog->Show(owner))) {
      IShellItem* result = nullptr;
      if (SUCCEEDED(dialog->GetResult(&result))) {
        PWSTR path = nullptr;
        if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
          chosen = fs::path(path);
          CoTaskMemFree(path);
        }
        result->Release();
      }
    }
    dialog->Release();
  }

  if (weStartedIt) CoUninitialize();
  return chosen;
}

struct LiveState {
  bool wowRunning = false;
  bool wowBorderless = false;
  uint32_t wowWidth = 0;
  uint32_t wowHeight = 0;
  bool overlayRunning = false;
  std::optional<SidecarStatus> status;
};

LiveState PollLiveState() {
  LiveState live;
  if (auto wow = FindWowWindow()) {
    live.wowRunning = true;
    live.wowBorderless = wow->borderless;
    live.wowWidth = static_cast<uint32_t>(wow->clientScreen.right - wow->clientScreen.left);
    live.wowHeight = static_cast<uint32_t>(wow->clientScreen.bottom - wow->clientScreen.top);
  }
  live.overlayRunning = control::IsRunning();
  if (live.overlayRunning) live.status = control::Read();
  return live;
}

enum class Section { Status, Setup, Checks, Tuning, Log, Count };

// The whole tuning surface, as four choices instead of nine sliders.
//
// Almost nobody wants to reason about transfer strength against paper-white
// scale. They want the good one, a gentler one, one that stops the smearing,
// and a way to see what it is doing at all. The sliders still exist under
// Advanced for the people who do -- these just set coherent bundles of them,
// so no combination anyone lands on by accident is one we have never tried.
struct Preset {
  const char* name;
  const char* summary;
  const char* detail;
  void (*apply)(Config&);
};

const Preset kPresets[] = {
    {"Recommended",
     "The tuned default. Start here.",
     "Full neural intensity with the CNN F render preset, which clamps temporal "
     "history hard -- the right choice when motion vectors are estimated from "
     "colour rather than rendered by the game.",
     [](Config& c) {
       c.neuralPass = "reshade";
       c.dlssPreset = "cnn-f";
       c.flowGridSize = 4;
       c.syntheticDepth = 0.5f;
       c.neural = NeuralSettings{};   // every add-on knob at its own default
     }},
    {"Softer",
     "Half strength. Use if the picture looks over-processed.",
     "The same pipeline with the neural result mixed in at 60%. Cheaper on the "
     "eyes for interface-heavy scenes, and the first thing to try if faces or "
     "text look waxy.",
     [](Config& c) {
       c.neuralPass = "reshade";
       c.dlssPreset = "cnn-f";
       c.flowGridSize = 4;
       c.syntheticDepth = 0.5f;
       c.neural = NeuralSettings{};
       c.neural.intensity = 0.60f;
     }},
    {"Most stable",
     "For smearing, or flicker on flames and lights.",
     "Switches to CNN E, which clamps temporal history hardest, and eases the "
     "intensity. This is the preset for when motion looks smeared -- the "
     "estimated motion vectors are being confidently wrong and this contains "
     "them.",
     [](Config& c) {
       c.neuralPass = "reshade";
       c.dlssPreset = "cnn-e";
       // Grid 2 rather than 4: a finer motion field is the one thing that
       // genuinely helps a smearing complaint, and it is worth the millisecond
       // in the preset whose whole job is stability.
       c.flowGridSize = 2;
       c.syntheticDepth = 0.5f;
       c.neural = NeuralSettings{};
       c.neural.intensity = 0.85f;
     }},
    {"Off (A/B baseline)",
     "Capture and present, untouched.",
     "No neural work at all, on the same capture and present path. This is the "
     "honest comparison: whatever you see here is what the overlay costs you "
     "before any neural rendering happens.",
     [](Config& c) { c.neuralPass = "passthrough"; }},
};

// Which preset the current config corresponds to, or npos when the operator has
// hand-edited their way off the map. Compared on the fields the presets set, so
// an unrelated change -- the HUD toggle, a mask rectangle -- does not read as
// "custom".
size_t MatchingPreset(const Config& config) {
  for (size_t i = 0; i < std::size(kPresets); ++i) {
    Config candidate;
    kPresets[i].apply(candidate);
    if (candidate.neuralPass != config.neuralPass) continue;
    if (candidate.neuralPass == "passthrough") return i;
    if (candidate.dlssPreset == config.dlssPreset &&
        candidate.flowGridSize == config.flowGridSize &&
        candidate.syntheticDepth == config.syntheticDepth &&
        candidate.neural.intensity == config.neural.intensity &&
        candidate.neural.colorStrength == config.neural.colorStrength &&
        candidate.neural.preset == config.neural.preset &&
        candidate.neural.style == config.neural.style &&
        candidate.neural.upscaling == config.neural.upscaling) {
      return i;
    }
  }
  return static_cast<size_t>(-1);
}

// The page's identity, in English, always. This is what the optional
// command-line argument is matched against, so translating it would break
// `wowsidecar-manager.exe Checks` in every language but one -- and that
// argument exists so the README's screenshots can be captured without driving
// the mouse.
const char* SectionId(Section section) {
  switch (section) {
    case Section::Status: return "Status";
    case Section::Setup:  return "Setup";
    case Section::Checks: return "Checks";
    case Section::Tuning: return "Tuning";
    default:              return "Log";
  }
}

// What the nav rail shows. Separate from the identity above on purpose.
const char* SectionLabel(Section section) { return Tr(SectionId(section)); }

bool AnyBlockingFailure(const std::vector<ProbeResult>& results) {
  for (const auto& r : results) {
    if (r.state == ProbeState::Fail) return true;
  }
  return false;
}

void DrawBoard(const std::vector<ProbeResult>& results) {
  if (!ImGui::BeginTable("probes", 3,
                         ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                             ImGuiTableFlags_SizingStretchProp)) {
    return;
  }
  ImGui::TableSetupColumn("state", ImGuiTableColumnFlags_WidthFixed, 88.0f);
  ImGui::TableSetupColumn("check", ImGuiTableColumnFlags_WidthFixed, 250.0f);
  ImGui::TableSetupColumn("detail", ImGuiTableColumnFlags_WidthStretch);

  for (const auto& r : results) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    if (g_fonts.caption) ImGui::PushFont(g_fonts.caption);
    ImGui::TextColored(StateColor(r.state, g_colors), "%s", StateLabel(r.state));
    if (g_fonts.caption) ImGui::PopFont();

    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(r.title.c_str());

    ImGui::TableSetColumnIndex(2);
    ImGui::TextWrapped("%s", r.detail.c_str());
    if (r.state != ProbeState::Ok && !r.remedy.empty()) Hint(r.remedy.c_str());
  }
  ImGui::EndTable();
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
  PinSystemGraphicsDlls();

  // Before the first window exists, which is the only time this can be set.
  //
  // Without it Windows treats the process as written for 96 DPI and scales the
  // finished pixels up, which is blurry. With it the window is given real
  // pixels and the interface has to do its own scaling -- which is the point:
  // text rasterised at the right size is sharp, text stretched to it is not.
  //
  // Per-monitor rather than system-wide because this machine is as likely to
  // have two displays at different scalings as one, and dragging the window
  // between them should not leave it the wrong size.
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  WNDCLASSEXW wc{sizeof(wc)};
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  // Resource 1, from src/sidecar.rc. The shell finds the icon in the file by
  // itself for Explorer and the taskbar, but the title bar and Alt-Tab take it
  // from the window class, and an unset class icon is the generic one Windows
  // hands out to a program that did not say.
  wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
  wc.hIconSm = wc.hIcon;
  wc.lpszClassName = L"SidecarManager";
  RegisterClassExW(&wc);

  const std::wstring windowTitle =
      WideFromUtf8("DLSS 5 \xE2\x80\x94 Sidecar for World of Warcraft \xE2\x80\x94 v" SIDECAR_VERSION);
  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, windowTitle.c_str(),
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              kWindowWidth, kWindowHeight, nullptr, nullptr,
                              inst, nullptr);
  if (!hwnd || !CreateDeviceAndSwapChain(hwnd)) {
    MessageBoxW(nullptr, L"Could not create a Direct3D 11 device.",
                L"DLSS 5 Sidecar", MB_ICONERROR);
    return 1;
  }
  // A Windows-accent title bar over a near-black panel looks like two different
  // applications stapled together.
  //
  // Dark mode alone does not fix it: with "show accent colour on title bars"
  // switched on -- which is common, and was on the machine this was developed
  // against -- the accent wins. So the caption is painted explicitly instead,
  // in the panel's own colours. Every attribute here is Windows 11; on anything
  // older the calls fail harmlessly and the bar stays as the system drew it.
  {
    const BOOL darkTitleBar = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* USE_IMMERSIVE_DARK_MODE */, &darkTitleBar,
                          sizeof(darkTitleBar));
    // COLORREF is 0x00BBGGRR, so these are the theme's colours byte-reversed.
    const COLORREF caption = 0x00120C0B;   // 0x0B0C12, the window background
    const COLORREF text = 0x00CEE0E8;      // parchment
    const COLORREF border = 0x006EAAC8;    // bronze
    DwmSetWindowAttribute(hwnd, 35 /* CAPTION_COLOR */, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, 36 /* TEXT_COLOR */, &text, sizeof(text));
    DwmSetWindowAttribute(hwnd, 34 /* BORDER_COLOR */, &border, sizeof(border));
  }
  ShowWindow(hwnd, show);

  const fs::path sidecarDir = ExecutableDirectory();
  const fs::path configPath = sidecarDir / "sidecar.toml";

  // Its own file: the runtime owns sidecar.log, and two processes writing one
  // file would interleave into nonsense.
  GlobalLog().OpenFile(sidecarDir / "sidecar-manager.log");
  GlobalLog().Info("manager " SIDECAR_VERSION " starting");

  std::error_code ec;
  bool noticeAcknowledged = fs::exists(configPath, ec) && !ec;

  // Read before the atlas is built, not after. The faces are rasterised at
  // their final size, so learning the scale afterwards would mean building the
  // whole atlas twice on every launch that is not at scale 1.
  std::vector<std::string> warnings;
  Config config;
  if (auto loaded = LoadConfig(configPath, warnings)) config = *loaded;
  for (const auto& warning : warnings) GlobalLog().Warn("config: " + warning);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;   // no state file next to the binary

  // How big to draw everything, decided here and not changed again.
  //
  // DPI alone is not the answer, and assuming it was is what made the first
  // attempt useless. A 49-inch display running at 100% scaling reports 96 DPI
  // and is telling the truth -- each pixel really is a normal size -- while the
  // interface on it is still unreadable, because the screen is a metre wide and
  // the person is sitting back from it. What DPI cannot express is how far away
  // somebody is, and physical size is the best proxy anyone has: a bigger
  // display is a display you sit further from.
  //
  // So: the DPI scaling Windows asks for, multiplied by how much wider the
  // panel is than the one this interface was drawn against. Both are needed.
  // A 4K laptop reports 200% DPI and a small panel, and gets 2.0 from the first
  // term alone; this display reports 100% and a huge panel, and gets its
  // increase from the second.
  const UINT dpi = GetDpiForWindow(hwnd);
  const float dpiScale = dpi > 0 ? static_cast<float>(dpi) / 96.0f : 1.0f;

  float sizeScale = 1.0f;
  if (const HDC screen = GetDC(hwnd)) {
    // Physical width in millimetres, from the display's own EDID.
    const int widthMm = GetDeviceCaps(screen, HORZSIZE);
    ReleaseDC(hwnd, screen);
    // 620 mm is a 28-inch 16:9 panel -- a large desktop monitor, and about
    // where this interface's sizes were chosen. Below that, no change: a
    // smaller screen is one you sit closer to, and shrinking the text would be
    // the wrong direction.
    if (widthMm > 620) {
      sizeScale = std::min(static_cast<float>(widthMm) / 620.0f, 2.0f);
    }
  }

  // The operator's own number wins outright when they have set one. Zero means
  // they have not, which is the default and the common case.
  float appliedScale = config.uiScale > 0.0f ? config.uiScale : dpiScale * sizeScale;
  appliedScale = std::clamp(appliedScale, 1.0f, 3.0f);
  GlobalLog().Info("interface scale " + std::to_string(appliedScale) + " (dpi " +
                   std::to_string(dpi) + ", " +
                   (config.uiScale > 0.0f ? "set in sidecar.toml" : "measured from the display") +
                   ")");
  g_scale = appliedScale;
  kNavWidth *= appliedScale;
  kHeaderHeight *= appliedScale;
  kPad *= appliedScale;

  g_fonts = LoadThemeFonts(appliedScale);   // before the backend builds its atlas
  ApplySidecarTheme(true);
  ImGui::GetStyle().ScaleAllSizes(appliedScale);
  g_colors = CurrentThemeColors(true);
  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplDX11_Init(g_device.Get(), g_context.Get());

  // The window was created at the design size, which on a scaled interface is
  // too small to hold it. Grown once, here, rather than in the creation call,
  // because neither the DPI nor the monitor is known until the window exists.
  //
  // Capped at the work area rather than simply multiplied. Multiplying asked
  // for 1574 pixels of height on a 1440-tall screen, and the bottom of the
  // interface went where nobody could reach it. The work area is what Windows
  // says is usable -- the desktop less the taskbar -- and nine tenths of it
  // leaves the window looking like a window rather than a full-screen takeover.
  if (appliedScale > 1.0f) {
    int width = static_cast<int>(kWindowWidth * appliedScale);
    int height = static_cast<int>(kWindowHeight * appliedScale);

    MONITORINFO monitor{sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
      const int availableWidth = monitor.rcWork.right - monitor.rcWork.left;
      const int availableHeight = monitor.rcWork.bottom - monitor.rcWork.top;
      width = std::min(width, availableWidth * 9 / 10);
      height = std::min(height, availableHeight * 9 / 10);
      SetWindowPos(hwnd, nullptr,
                   monitor.rcWork.left + (availableWidth - width) / 2,
                   monitor.rcWork.top + (availableHeight - height) / 2,
                   width, height, SWP_NOZORDER);
    } else {
      SetWindowPos(hwnd, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);
    }
  }


  // Guess the folder only when nothing is set. A path the operator chose
  // outranks the registry on every later launch, and because an empty setting
  // is omitted from the file entirely, "never configured" and "cleared" are the
  // same state on purpose: both mean "guess for me".
  if (config.wowDir.empty()) {
    if (const auto detected = DetectWowFolder()) {
      config.wowDir = Utf8FromPath(*detected);
      GlobalLog().Info("WoW folder detected: " + config.wowDir);
    } else {
      GlobalLog().Info("no WoW folder detected; the injector scan needs one to run");
    }
  }

  {
    Language stored = Language::English;
    if (ParseLanguageTag(config.language, stored)) SetLanguage(stored);
  }

  auto results = RunAllProbes(sidecarDir, PathFromUtf8(config.wowDir));

  // Which page opens first. An optional command-line argument names it, which
  // exists so the README's screenshots can be captured without driving the
  // mouse; without one it opens on Status, where anyone launching the app
  // wants to be.
  Section section = Section::Status;
  {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 2) {
      for (int i = 0; i < static_cast<int>(Section::Count); ++i) {
        const auto candidate = static_cast<Section>(i);
        if (_wcsicmp(argv[1], std::wstring(SectionId(candidate), SectionId(candidate) +
                                                                    strlen(SectionId(candidate)))
                                  .c_str()) == 0) {
          section = candidate;
        }
      }
    }
    if (argv) LocalFree(argv);
  }
  bool dirty = false;
  std::string setupMessage;
  bool setupMessageIsError = false;
  bool confirmUninstall = false;
  bool uninstallGenerated = false;

  // Which advisory the Status page is showing, held across frames rather than
  // recomputed from a bare threshold each time. A frame rate hovering on a
  // threshold would otherwise flip the message on and off several times a
  // second; these are switched with hysteresis instead.
  bool captureBound = false;
  bool starving = false;

  // The overlay's log, tailed from disk. It belongs to the other process, so
  // there is nothing to subscribe to -- it is read, on a timer, while the Log
  // page is open.
  std::vector<std::string> overlayLog;
  double overlayLogReadAt = -1.0;

  const auto save = [&]() {
    if (SaveConfig(configPath, config)) {
      dirty = false;
      GlobalLog().Info("settings saved");
      WriteNeuralSettings(sidecarDir / "ReShade.ini", config.neural);
    } else {
      GlobalLog().Error("could not write sidecar.toml");
    }
  };

  const auto startOverlay = [&]() {
    if (dirty) save();
    // The runtime has to be able to hand the foreground back to the game, and a
    // launched process only inherits that right from the process that owned the
    // foreground -- which, at this instant, is this one.
    AllowSetForegroundWindow(ASFW_ANY);
    ShellExecuteW(nullptr, L"open", (sidecarDir / "wowsidecar.exe").c_str(), nullptr,
                  sidecarDir.c_str(), SW_SHOWNORMAL);
    GlobalLog().Info("overlay launch requested");
    // And then get out of the way: the overlay is about to cover the screen, and
    // this window would otherwise sit between it and the game.
    ShowWindow(hwnd, SW_MINIMIZE);
  };

  MSG msg{};
  while (msg.message != WM_QUIT) {
    if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
      continue;
    }

    const LiveState live = PollLiveState();
    const bool blocked = AnyBlockingFailure(results);
    size_t missingComponents = 0;
    for (const auto& component : Components()) {
      if (!component.required) continue;
      if (!fs::exists(sidecarDir / std::string(component.installedAs), ec) || ec) {
        ++missingComponents;
      }
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("shell", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    // ------------------------------------------------------------- header band
    ImGui::BeginChild("header", ImVec2(0.0f, kHeaderHeight));
    ImGui::Indent(kPad);
    ImGui::Dummy(ImVec2(0.0f, S(12.0f)));

    if (g_fonts.title) ImGui::PushFont(g_fonts.title);
    ImGui::TextColored(Rgb(g_colors.goldBright), "DLSS 5 Sidecar");
    if (g_fonts.title) ImGui::PopFont();

    if (g_fonts.caption) ImGui::PushFont(g_fonts.caption);
    ImGui::TextDisabled(Tr("Neural rendering for World of Warcraft, from outside the game process"));
    if (g_fonts.caption) ImGui::PopFont();

    // The primary action lives in the header and never moves, so it is in the
    // same place no matter which section is open.
    //
    // Its width is measured, not chosen. "Start overlay" and "Запустить
    // оверлей" are not the same length, and neither is either of them at a
    // different interface scale; a constant here is a constant that is wrong
    // for every case but the one it was measured against.
    const ImGuiStyle& style = ImGui::GetStyle();
    const auto widestOf = [](std::initializer_list<const char*> texts) {
      float widest = 0.0f;
      for (const char* text : texts) {
        widest = std::max(widest, ImGui::CalcTextSize(text).x);
      }
      return widest;
    };

    // Both labels, because the button swaps between them and must not resize
    // under the cursor when the overlay starts.
    const float buttonWidth =
        widestOf({Tr("Start overlay"), Tr("Stop overlay")}) + style.FramePadding.x * 2.0f + S(28.0f);

    // Each language names itself. "Russian" in an English list is no help to
    // someone who cannot read the list. Declared here so the combo can be
    // measured before the button is placed.
    const char* languageNames[] = {"English", "Русский"};
    // The arrow is a square the height of the frame, and it is drawn inside the
    // width the combo is given rather than beside it.
    const float languageWidth = widestOf({languageNames[0], languageNames[1]}) +
                                ImGui::GetFrameHeight() + style.FramePadding.x * 2.0f + S(10.0f);

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth - kPad);
    ImGui::SetCursorPosY(S(30.0f));

    if (!noticeAcknowledged) ImGui::BeginDisabled();
    if (live.overlayRunning) {
      ImGui::PushStyleColor(ImGuiCol_Button, Rgb(g_colors.fail, 0.20f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Rgb(g_colors.fail, 0.36f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, Rgb(g_colors.fail, 0.50f));
      if (ImGui::Button(Tr("Stop overlay"), ImVec2(buttonWidth, S(42.0f)))) {
        if (!control::Send(SidecarCommand::Stop)) {
          GlobalLog().Warn("the overlay did not answer; it may already be closing");
        }
      }
      ImGui::PopStyleColor(3);
    } else {
      const bool canStart = !blocked && live.wowRunning;
      if (!canStart) ImGui::BeginDisabled();
      ImGui::PushStyleColor(ImGuiCol_Button, Rgb(g_colors.accent, 0.26f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Rgb(g_colors.accent, 0.42f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, Rgb(g_colors.goldBright, 0.55f));
      ImGui::PushStyleColor(ImGuiCol_Text, Rgb(g_colors.goldBright));
      if (ImGui::Button(Tr("Start overlay"), ImVec2(buttonWidth, S(42.0f)))) startOverlay();
      ImGui::PopStyleColor(4);
      if (!canStart) ImGui::EndDisabled();
    }
    if (!noticeAcknowledged) ImGui::EndDisabled();

    // Left of the primary action, and outside the notice's disabled block: a
    // person who cannot read the notice has to be able to change the language
    // before agreeing to it.
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth - kPad - languageWidth -
                         S(16.0f));
    ImGui::SetCursorPosY(S(36.0f));
    ImGui::SetNextItemWidth(languageWidth);
    int languageIndex = CurrentLanguage() == Language::Russian ? 1 : 0;
    if (ImGui::Combo("##language", &languageIndex, languageNames, 2)) {
      const Language chosen = languageIndex == 1 ? Language::Russian : Language::English;
      SetLanguage(chosen);
      config.language = TagForLanguage(chosen);
      // Saved immediately rather than left to the Save button on another page:
      // this is a preference about the tool, not a setting for the pipeline,
      // and losing it on exit would make the switch look broken.
      save();
      // The board holds finished strings rather than keys -- a probe's detail
      // is often a sentence with a driver version built into it, which no
      // lookup could translate after the fact. So the probes run again. They
      // are cheap, and the alternative is half an interface in each language.
      results = RunAllProbes(sidecarDir, PathFromUtf8(config.wowDir));
    }


    ImGui::Unindent(kPad);
    ImGui::EndChild();

    ImGui::Indent(kPad);
    ImGui::PushItemWidth(-kPad);
    GoldRule(0.55f, 8.0f);
    ImGui::PopItemWidth();
    ImGui::Unindent(kPad);

    if (!noticeAcknowledged) ImGui::BeginDisabled();

    // ---------------------------------------------------------------- nav rail
    ImGui::Indent(kPad);
    ImGui::BeginChild("nav", ImVec2(kNavWidth, -kPad));

    // A gutter the width of the marker, so the marker has somewhere to be that
    // is not on top of the first letter. Applied to every row rather than only
    // the selected one: text that shifts sideways when a row is chosen reads as
    // a glitch.
    const float markerGutter = S(12.0f);
    ImGui::Indent(markerGutter);

    for (int i = 0; i < static_cast<int>(Section::Count); ++i) {
      const auto candidate = static_cast<Section>(i);
      const bool selected = candidate == section;
      if (selected) ImGui::PushStyleColor(ImGuiCol_Text, Rgb(g_colors.goldBright));
      const ImVec2 rowAt = ImGui::GetCursorScreenPos();
      ImGui::PushID(SectionId(candidate));
      if (ImGui::Selectable(SectionLabel(candidate), selected,
                            ImGuiSelectableFlags_None, ImVec2(0.0f, S(34.0f)))) {
        section = candidate;
      }
      if (selected) {
        ImGui::PopStyleColor();
        // A gold bar down the left edge of the selected row. The tinted
        // background alone is nearly invisible against these panels, and "which
        // page am I on" should never need a second look.
        //
        // Drawn just inside the row, not just outside it: anything left of the
        // child's content edge is clipped away and silently never appears.
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(rowAt.x - markerGutter, ImGui::GetItemRectMin().y),
            ImVec2(rowAt.x - markerGutter + S(3.0f), ImGui::GetItemRectMax().y),
            ImGui::GetColorU32(Rgb(g_colors.goldBright)));
      }

      // A section that needs attention says so from the rail, so nobody has to
      // go looking through every page to find the one thing that is wrong.
      const char* badge = nullptr;
      ImVec4 badgeColor = Rgb(g_colors.warn);
      if (candidate == Section::Setup && missingComponents > 0) {
        badge = "!";
        badgeColor = Rgb(g_colors.fail);
      } else if (candidate == Section::Checks && blocked) {
        badge = "!";
        badgeColor = Rgb(g_colors.fail);
      } else if (candidate == Section::Tuning && dirty) {
        badge = "*";
      }
      if (badge) {
        ImGui::SameLine();
        ImGui::SetCursorPosX(kNavWidth - 26.0f);
        ImGui::TextColored(badgeColor, "%s", badge);
      }
      ImGui::PopID();
    }

    // The rail's foot is where the live state belongs: it is true regardless of
    // which page is open.
    ImGui::Unindent(markerGutter);
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - S(124.0f));
    GoldRule(0.30f, 8.0f);
    if (g_fonts.caption) ImGui::PushFont(g_fonts.caption);
    Dot(live.wowRunning && live.wowBorderless,
        live.wowRunning ? (live.wowBorderless ? "WoW: borderless" : "WoW: not borderless")
                        : "WoW: not running");
    Dot(live.overlayRunning, live.overlayRunning ? "Overlay: running" : "Overlay: stopped");
    if (live.wowRunning) ImGui::TextDisabled("   %ux%u", live.wowWidth, live.wowHeight);
    // Which build this is. At the foot of the rail rather than in a dialog,
    // because the question is always "what am I looking at" and never "let me
    // go and find out".
    ImGui::TextDisabled("   v" SIDECAR_VERSION);
    if (g_fonts.caption) ImGui::PopFont();
    ImGui::EndChild();

    ImGui::SameLine(0.0f, kPad);
    ImGui::BeginChild("content", ImVec2(-kPad, -kPad));

    // ---------------------------------------------------------------- sections
    if (section == Section::Status) {
      SectionHeading("Live");
      if (live.status) {
        const auto& s = *live.status;
        const GateVerdict verdict = JudgeGate(s.p99Ms);
        const ProbeState asState = verdict == GateVerdict::Playable   ? ProbeState::Ok
                                   : verdict == GateVerdict::Marginal ? ProbeState::Warn
                                                                      : ProbeState::Fail;
        char fps[32], p50[32], p99[32], resolution[32];
        std::snprintf(fps, sizeof(fps), "%.0f", s.fps);
        std::snprintf(p50, sizeof(p50), "%.2f ms", s.p50Ms);
        std::snprintf(p99, sizeof(p99), "%.2f ms", s.p99Ms);
        std::snprintf(resolution, sizeof(resolution), "%ux%u", s.width, s.height);

        const float cardWidth = (ImGui::GetContentRegionAvail().x - 30.0f) / 4.0f;
        // Frame rate first. It is the number anyone actually asks about, and it
        // is not the same as 1/p50 -- the gap between them is idle time.
        StatCard("OVERLAY FPS", fps, cardWidth,
                 Rgb(s.fps >= 90.0 ? g_colors.ok
                     : s.fps >= 55.0 ? g_colors.warn
                                     : g_colors.fail));
        ImGui::SameLine();
        StatCard("LATENCY p50", p50, cardWidth, Rgb(g_colors.parchment));
        ImGui::SameLine();
        StatCard("LATENCY p99", p99, cardWidth, StateColor(asState, g_colors));
        ImGui::SameLine();
        char captured[32];
        std::snprintf(captured, sizeof(captured), "%.0f", s.captureFps);
        StatCard("CAPTURED FPS", captured, cardWidth, Rgb(g_colors.parchment));

        // The controls come before the diagnosis, and the diagnosis lives in a
        // fixed-height box.
        //
        // These advisories appear and disappear as the numbers move across a
        // threshold, and anything that changes height moves every control below
        // it. With the buttons underneath, they slid out from under the cursor
        // mid-click. Buttons first, then a region whose height never changes
        // whatever it has to say.
        ImGui::Dummy(ImVec2(0.0f, S(10.0f)));
        const bool visible = s.overlayVisible != 0;
        if (ImGui::Button(visible ? Tr("Hide overlay (A/B compare)") : Tr("Show overlay"),
                          ImVec2(S(230.0f), S(34.0f)))) {
          control::Send(visible ? SidecarCommand::HideOverlay : SidecarCommand::ShowOverlay);
        }
        ImGui::SameLine();
        const bool hudUp = s.hudVisible != 0;
        if (ImGui::Button(hudUp ? Tr("Hide HUD") : Tr("Show HUD"), ImVec2(S(150.0f), S(34.0f)))) {
          control::Send(hudUp ? SidecarCommand::HideHud : SidecarCommand::ShowHud);
        }
        ImGui::SameLine();
        ImGui::TextDisabled(Tr("Hiding the overlay uncovers the untouched game without "
                            "stopping capture."));

        // Hysteresis, so an advisory does not strobe while the frame rate sits
        // on its threshold. Each condition turns on and off at different values,
        // and the gap between them is wide enough to cover the jitter.
        if (s.captureFps > 0.0) {
          const double share = s.fps / s.captureFps;
          if (share >= 0.92) captureBound = true;
          if (share < 0.86) captureBound = false;
          if (share < 0.72) starving = true;
          if (share >= 0.80) starving = false;
        }
        const bool spilling = s.vramSpilledMb > 0;
        const bool overBudget = s.vramBudgetMb > 0 && s.vramUsedMb > s.vramBudgetMb;

        ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
        ImGui::BeginChild("advice", ImVec2(0.0f, S(132.0f)), ImGuiChildFlags_Border);
        ImGui::Dummy(ImVec2(0.0f, S(2.0f)));
        ImGui::Indent(S(10.0f));
        if (spilling || overBudget) {
          // Memory first: when this is the problem every other reading is a
          // consequence of it, including the GPU wait in the breakdown below.
          ImGui::TextColored(Rgb(g_colors.fail),
                             "Out of GPU memory -- %u MB pushed into system RAM.",
                             s.vramSpilledMb);
          Hint("The card is full, so the driver is moving resources to system "
               "memory and every frame waits on the PCIe bus. It shows below as "
               "a huge GPU wait with the GPU nearly idle, which is easy to "
               "mistake for a slow neural pass.\n\n"
               "Close what else is using the card -- browsers, streaming and "
               "capture tools, anything compositing a second monitor -- and lower "
               "the game's texture quality. Nothing in this tool can make room.");
        } else if (captureBound) {
          ImGui::TextColored(Rgb(g_colors.warn),
                             "Windows is only capturing %.0f frames a second, so that is "
                             "the ceiling.", s.captureFps);
          Hint("This is the desktop compositor's rate, not the sidecar's and not "
               "the game's -- the game may well be running far faster. It is "
               "usually set by your monitor's refresh rate and, on a multi-monitor "
               "setup with mismatched refresh rates, by the slowest one. Nothing "
               "in this tool can raise it.");
        } else if (starving) {
          ImGui::TextColored(Rgb(g_colors.warn),
                             "Only presenting %.0f of the %.0f frames captured.", s.fps,
                             s.captureFps);
          Hint("The game is competing for the GPU. Measured on the development "
               "machine, the same scene ran at 11.7 fps with the game focused and "
               "35.2 fps with it in the background, purely because an unfocused "
               "game throttles itself and hands the card back.\n\n"
               "Cap the game's frame rate. This costs nothing: the overlay can "
               "never show more than the capture rate above, so every frame the "
               "game renders beyond that is discarded before it reaches here.");
        } else {
          ImGui::TextColored(Rgb(g_colors.ok), "Keeping up with capture.");
          Hint("The overlay is presenting close to every frame Windows hands it, "
               "and there is memory to spare. Nothing to fix here.");
        }
        ImGui::Unindent(S(10.0f));
        ImGui::EndChild();

        // Memory, as a plain reading. The advisory above owns the alarm; this is
        // just the number, and its height never changes.
        if (s.vramBudgetMb > 0) {
          const float ratio = static_cast<float>(s.vramUsedMb) /
                              static_cast<float>(s.vramBudgetMb);
          ImGui::Dummy(ImVec2(0.0f, S(12.0f)));
          if (g_fonts.caption) ImGui::PushFont(g_fonts.caption);
          ImGui::TextDisabled(Tr("GPU MEMORY USED BY THE SIDECAR"));
          if (g_fonts.caption) ImGui::PopFont();
          ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                                (spilling || overBudget) ? Rgb(g_colors.fail)
                                : ratio > 0.9f           ? Rgb(g_colors.warn)
                                                         : Rgb(g_colors.accent));
          char label[96];
          std::snprintf(label, sizeof(label), "%u MB used  /  %u MB allowed%s",
                        s.vramUsedMb, s.vramBudgetMb,
                        spilling ? Tr("  --  SPILLING") : "");
          ImGui::ProgressBar(ratio > 1.0f ? 1.0f : ratio, ImVec2(S(-1.0f), S(20.0f)), label);
          ImGui::PopStyleColor();
          if (g_fonts.caption) ImGui::PushFont(g_fonts.caption);
          ImGui::TextDisabled(Tr("This is the sidecar's own share, not the whole card. "
                              "More of it would not be faster -- the pass allocates "
                              "what it needs. The number that matters is whether any "
                              "has spilled."));
          if (g_fonts.caption) ImGui::PopFont();
        }

        // Where the frame went, as a stacked bar. A frame rate on its own tells
        // you something is slow; this tells you which of four different things
        // it is, and they have four different answers.
        ImGui::Dummy(ImVec2(0.0f, S(12.0f)));
        if (g_fonts.caption) ImGui::PushFont(g_fonts.caption);
        ImGui::TextDisabled(Tr("WHERE THE FRAME GOES"));
        if (g_fonts.caption) ImGui::PopFont();

        const double total = s.idleMs + s.recordMs + s.presentWaitMs + s.gpuWaitMs;
        if (total > 0.0) {
          const struct { const char* label; double ms; unsigned int colour; } parts[] = {
              {"GPU (the neural pass)", s.gpuWaitMs, g_colors.epic},
              {"waiting for the game", s.idleMs, 0x4A5568},
              {"our CPU work", s.recordMs, g_colors.accent},
              {"compositor pacing", s.presentWaitMs, g_colors.warn},
          };
          const float barWidth = ImGui::GetContentRegionAvail().x - 4.0f;
          const ImVec2 barAt = ImGui::GetCursorScreenPos();
          ImDrawList* draw = ImGui::GetWindowDrawList();
          float x = barAt.x;
          for (const auto& part : parts) {
            const float w = static_cast<float>(part.ms / total) * barWidth;
            draw->AddRectFilled(ImVec2(x, barAt.y), ImVec2(x + w, barAt.y + S(18.0f)),
                                ImGui::GetColorU32(Rgb(part.colour, 0.85f)));
            x += w;
          }
          ImGui::Dummy(ImVec2(0.0f, S(24.0f)));

          if (g_fonts.caption) ImGui::PushFont(g_fonts.caption);
          for (const auto& part : parts) {
            // Drawn, not typed. ImGui builds its atlas over Basic Latin by
            // default, so a box-drawing glyph comes out as a literal "?".
            const ImVec2 swatch = ImGui::GetCursorScreenPos();
            const float size = ImGui::GetTextLineHeight() * 0.62f;
            const float top = swatch.y + (ImGui::GetTextLineHeight() - size) * 0.5f;
            draw->AddRectFilled(ImVec2(swatch.x, top), ImVec2(swatch.x + size, top + size),
                                ImGui::GetColorU32(Rgb(part.colour)));
            ImGui::Dummy(ImVec2(size + S(8.0f), 0.0f));
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::Text(Tr("%-24s %5.2f ms"), part.label, part.ms);
          }
          if (g_fonts.caption) ImGui::PopFont();
        }

        ImGui::Dummy(ImVec2(0.0f, S(12.0f)));
        const bool neural = std::string(s.passName).find("reshade") != std::string::npos;
        ImGui::TextUnformatted(Tr("Frames"));
        ImGui::SameLine(S(120.0f));
        ImGui::Text(Tr("%llu presented, %llu dropped"),
                    static_cast<unsigned long long>(s.frames),
                    static_cast<unsigned long long>(s.drops));
        ImGui::TextUnformatted(Tr("Pass"));
        ImGui::SameLine(S(120.0f));
        ImGui::TextColored(neural ? Rgb(g_colors.epic) : Rgb(g_colors.parchment), "%s",
                           s.passName);
        if (*s.runtimeVariant) {
          ImGui::TextUnformatted(Tr("Runtime"));
          ImGui::SameLine(S(120.0f));
          ImGui::TextUnformatted(s.runtimeVariant);
        }
        ImGui::TextUnformatted(Tr("Verdict"));
        ImGui::SameLine(S(120.0f));
        ImGui::TextColored(StateColor(asState, g_colors), "%s",
                           verdict == GateVerdict::Playable   ? Tr("playable")
                           : verdict == GateVerdict::Marginal ? Tr("marginal")
                                                              : Tr("too slow"));

        if (*s.lastError) {
          ImGui::Dummy(ImVec2(0.0f, S(10.0f)));
          ImGui::TextColored(Rgb(g_colors.fail), "The overlay reported");
          ImGui::TextWrapped("%s", s.lastError);
        }

      } else if (live.overlayRunning) {
        ImGui::TextDisabled(Tr("The overlay is starting. Numbers appear after the first "
                            "few frames."));
      } else {
        ImGui::TextDisabled(Tr("Nothing running."));
        Hint(blocked      ? "Some checks are failing. Open Checks to see what."
             : missingComponents > 0
                 ? "Files are missing. Open Setup to install them."
             : !live.wowRunning
                 ? "Start World of Warcraft in borderless windowed mode, then press "
                   "Start overlay."
                 : "Press Start overlay.");
      }

      ImGui::Dummy(ImVec2(0.0f, S(20.0f)));
      SectionHeading("Playing with the overlay up");
      Hint("The overlay covers the game completely and passes every click "
           "straight through to it, so play normally. It never takes focus, so "
           "whatever had the keyboard keeps it -- if the game is not responding, "
           "alt-tab to World of Warcraft once.\n\n"
           "Ctrl+Alt+Backspace takes the overlay down from anywhere, without "
           "needing this window.");
    }

    if (section == Section::Setup) {
      SectionHeading("Required files");
      Hint("Three files have to sit next to the sidecar. None of them is ours "
           "to redistribute and nothing here downloads anything -- neither "
           "binary in this project can reach the network at all. Fetch them "
           "yourself, then point this at them.");
      ImGui::Dummy(ImVec2(0.0f, S(10.0f)));

      const auto& components = Components();
      for (size_t i = 0; i < components.size(); ++i) {
        const auto& component = components[i];
        const fs::path installed = sidecarDir / std::string(component.installedAs);
        const bool present = fs::exists(installed, ec) && !ec;

        ImGui::PushID(static_cast<int>(i));
        ImGui::BeginChild("row", ImVec2(0.0f, S(132.0f)), ImGuiChildFlags_Border);
        ImGui::Dummy(ImVec2(0.0f, S(2.0f)));
        ImGui::Indent(S(12.0f));

        Dot(present, std::string(component.title).c_str(), !component.required);
        ImGui::SameLine();
        if (g_fonts.caption) ImGui::PushFont(g_fonts.caption);
        ImGui::TextColored(present ? Rgb(g_colors.ok)
                           : component.required ? Rgb(g_colors.fail)
                                                : Rgb(g_colors.warn),
                           "  %s", present            ? Tr("INSTALLED")
                                   : component.required ? Tr("MISSING")
                                                        : Tr("OPTIONAL"));
        if (g_fonts.caption) ImGui::PopFont();

        Hint(std::string(component.purpose).c_str());
        if (g_fonts.caption) ImGui::PushFont(g_fonts.caption);
        ImGui::TextDisabled(Tr("Wanted as %s   |   Source: %s"),
                            std::string(component.installedAs).c_str(),
                            std::string(component.source).c_str());
        if (g_fonts.caption) ImGui::PopFont();

        if (ImGui::Button(present ? Tr("Replace...") : Tr("Choose file..."), ImVec2(S(150.0f), 0.0f))) {
          const fs::path picked =
              AskForFile(hwnd, L"DLL and add-on files\0*.dll;*.addon64\0All files\0*.*\0\0",
                         L"Choose the file to install");
          if (!picked.empty()) {
            const std::string name = picked.filename().string();
            if (!FileMatchesComponent(component, name)) {
              setupMessage = name + " is not what this slot wants (" +
                             std::string(component.installedAs) + ").";
              setupMessageIsError = true;
            } else {
              const auto result = InstallComponent(component, picked, sidecarDir);
              setupMessage = result.message;
              setupMessageIsError = !result.ok;
              if (result.ok) {
                GlobalLog().Info(result.message);
                results = RunAllProbes(sidecarDir, PathFromUtf8(config.wowDir));
              }
            }
          }
        }
        ImGui::Unindent(S(12.0f));
        ImGui::EndChild();
        ImGui::PopID();
      }

      if (!setupMessage.empty()) {
        ImGui::TextColored(setupMessageIsError ? Rgb(g_colors.fail) : Rgb(g_colors.ok),
                           "%s", setupMessage.c_str());
      }

      ImGui::Dummy(ImVec2(0.0f, S(14.0f)));
      SectionHeading("Remove");
      Hint("Deletes the files listed above from this folder, and nothing else. "
           "It never touches a WoW installation. The sidecar's own two "
           "executables stay; delete the folder to be rid of them.");
      ImGui::Checkbox(Tr("Also remove settings and logs"), &uninstallGenerated);
      ImGui::Checkbox(Tr("Yes, remove them"), &confirmUninstall);
      const auto plan = UninstallPlan(sidecarDir, uninstallGenerated);
      const bool canRemove = confirmUninstall && !plan.empty() && !live.overlayRunning;
      if (!canRemove) ImGui::BeginDisabled();
      if (ImGui::Button(Tr("Remove installed files"), ImVec2(S(210.0f), 0.0f))) {
        const auto result = RemoveAll(plan);
        setupMessage = result.message;
        setupMessageIsError = !result.ok;
        GlobalLog().Info(result.message);
        confirmUninstall = false;
        results = RunAllProbes(sidecarDir, PathFromUtf8(config.wowDir));
      }
      if (!canRemove) ImGui::EndDisabled();
      ImGui::SameLine();
      if (live.overlayRunning) {
        ImGui::TextDisabled(Tr("Stop the overlay first -- the files are in use."));
      } else if (!plan.empty()) {
        ImGui::TextDisabled(Tr("%zu file(s) would go."), plan.size());
      }
    }

    if (section == Section::Checks) {
      SectionHeading("System checks");
      Hint("Nothing here opens, reads, writes or hooks the game process. The "
           "import table of every binary is checked against that claim at "
           "build time.");
      ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
      ImGui::TextUnformatted(Tr("WoW folder (used only to scan filenames for injectors)"));
      ImGui::SetNextItemWidth(S(-260.0f));
      // Bound to the std::string itself, which grows as needed. The fixed
      // buffer this replaced silently truncated anything past its length, and a
      // path that has been cut short is a path that scans the wrong folder.
      if (ImGui::InputText("##wowdir", &config.wowDir)) dirty = true;
      ImGui::SameLine();
      if (ImGui::Button(Tr("Browse..."), ImVec2(S(110.0f), 0.0f))) {
        const fs::path picked =
            AskForFolder(hwnd, L"Select the folder that holds Wow.exe",
                         PathFromUtf8(config.wowDir));
        if (!picked.empty()) {
          config.wowDir = Utf8FromPath(picked);
          save();
          results = RunAllProbes(sidecarDir, picked);
        }
      }
      ImGui::SameLine();
      if (ImGui::Button(Tr("Detect"), ImVec2(S(90.0f), 0.0f))) {
        if (const auto detected = DetectWowFolder()) {
          config.wowDir = Utf8FromPath(*detected);
          save();
          results = RunAllProbes(sidecarDir, *detected);
        } else {
          // Saying nothing here would read as a dead button. The honest answer
          // is that the registry has no entry, not that the game is absent.
          GlobalLog().Warn(
              "no WoW folder found in the registry -- point at it with Browse");
        }
      }
      Hint("Detected from Battle.net's own uninstall entry. Point this at the "
           "folder Wow.exe sits in, not its parent: an injector has to be next "
           "to the executable to be loaded by it. Nothing inside is opened -- "
           "only the file names are read.");
      if (ImGui::Button(Tr("Re-run checks"), ImVec2(S(160.0f), 0.0f))) {
        results = RunAllProbes(sidecarDir, PathFromUtf8(config.wowDir));
        int failures = 0;
        for (const auto& r : results) {
          if (r.state == ProbeState::Fail) ++failures;
        }
        if (failures > 0) {
          GlobalLog().Error(std::to_string(failures) + " check(s) failing");
        } else {
          GlobalLog().Info("all checks green");
        }
      }
      ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
      DrawBoard(results);
    }

    if (section == Section::Tuning) {
      SectionHeading("Choose a look");
      Hint("Pick one. Every setting below is chosen for you, and the combinations "
           "here are ones that have actually been run -- unlike most of the "
           "arrangements you can reach by moving sliders individually.");
      ImGui::Dummy(ImVec2(0.0f, S(8.0f)));

      const size_t active = MatchingPreset(config);
      for (size_t i = 0; i < std::size(kPresets); ++i) {
        const bool selected = i == active;
        ImGui::PushID(static_cast<int>(i));

        // The whole card carries the state, not a sliver at its edge. A border
        // and a ground the operator can see from across the room beats a
        // three-pixel rule and a shade of gold.
        if (selected) {
          ImGui::PushStyleColor(ImGuiCol_Border, Rgb(g_colors.goldBright, 0.95f));
          ImGui::PushStyleColor(ImGuiCol_ChildBg, Rgb(g_colors.accent, 0.18f));
          ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, S(2.0f));
        }
        ImGui::BeginChild("preset", ImVec2(0.0f, S(104.0f)), ImGuiChildFlags_Border);
        ImGui::Dummy(ImVec2(0.0f, S(2.0f)));
        ImGui::Indent(S(12.0f));

        if (g_fonts.heading) ImGui::PushFont(g_fonts.heading);
        ImGui::TextColored(selected ? Rgb(g_colors.goldBright) : Rgb(g_colors.parchment),
                           "%s", Tr(kPresets[i].name));
        if (g_fonts.heading) ImGui::PopFont();

        // Said in words as well as in colour. Somebody who cannot pick the
        // chosen card out of four by its border should not have to.
        if (selected) {
          ImGui::SameLine();
          if (g_fonts.caption) ImGui::PushFont(g_fonts.caption);
          ImGui::TextColored(Rgb(g_colors.ok), "  %s", Tr("IN USE"));
          if (g_fonts.caption) ImGui::PopFont();
        }

        ImGui::TextUnformatted(Tr(kPresets[i].summary));
        Hint(kPresets[i].detail);

        ImGui::Unindent(S(12.0f));
        ImGui::EndChild();

        // Hover plus a click, rather than IsItemClicked on the child: a child
        // window with content of its own does not reliably report the click,
        // and a preset that silently refuses to be picked is worse than one
        // that is merely hard to see.
        // Against the card's own rectangle rather than through IsItemHovered.
        // A child window's hover state is about the child, and the flag that
        // would have widened it -- ChildWindows -- is only legal on
        // IsWindowHovered; passing it to IsItemHovered puts ImGui's own error
        // panel over the interface, which is what it is for.
        const bool hovered =
            ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !selected) {
          kPresets[i].apply(config);
          dirty = true;
        }
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        if (selected) {
          ImGui::PopStyleVar();
          ImGui::PopStyleColor(2);
        }
        ImGui::PopID();
      }
      if (active == static_cast<size_t>(-1)) {
        // Said loudly, because "none of these is highlighted" is otherwise
        // indistinguishable from "the highlight is broken".
        if (g_fonts.heading) ImGui::PushFont(g_fonts.heading);
        ImGui::TextColored(Rgb(g_colors.warn), "%s",
                           Tr("Custom -- your settings do not match any preset."));
        if (g_fonts.heading) ImGui::PopFont();
        Hint("Click one above to go back to a tested combination. Nothing is "
             "selected because the individual settings below have been moved "
             "away from all four.");
      }

      ImGui::Dummy(ImVec2(0.0f, S(14.0f)));

      // An unsaved change is easy to walk away from here: nothing on this page
      // takes effect until it is written, and the file is only read again at
      // the overlay's next start. So the ask is a band across the page in the
      // warning colour rather than four small words beside a button.
      if (dirty) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Rgb(g_colors.warn, 0.16f));
        ImGui::PushStyleColor(ImGuiCol_Border, Rgb(g_colors.warn, 0.75f));
        ImGui::BeginChild("unsaved", ImVec2(0.0f, S(58.0f)), ImGuiChildFlags_Border);
        ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
        ImGui::Indent(S(12.0f));
        if (g_fonts.heading) ImGui::PushFont(g_fonts.heading);
        ImGui::TextColored(Rgb(g_colors.warn), "%s", Tr("Changes are not saved yet."));
        if (g_fonts.heading) ImGui::PopFont();
        if (g_fonts.caption) ImGui::PushFont(g_fonts.caption);
        ImGui::TextDisabled("%s",
                            Tr("Nothing here reaches the picture until it is written to "
                               "sidecar.toml."));
        if (g_fonts.caption) ImGui::PopFont();
        ImGui::Unindent(S(12.0f));
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
      }

      const float saveWidth =
          ImGui::CalcTextSize(Tr("Save settings")).x + ImGui::GetStyle().FramePadding.x * 2.0f +
          S(28.0f);
      if (!dirty) ImGui::BeginDisabled();
      // Filled while there is something to save, so the button is the brightest
      // thing on the page exactly when it needs pressing.
      if (dirty) {
        ImGui::PushStyleColor(ImGuiCol_Button, Rgb(g_colors.accent, 0.55f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Rgb(g_colors.goldBright, 0.65f));
      }
      ImGui::PushStyleColor(ImGuiCol_Text, Rgb(g_colors.goldBright));
      if (ImGui::Button(Tr("Save settings"), ImVec2(saveWidth, S(38.0f)))) save();
      ImGui::PopStyleColor(dirty ? 3 : 1);
      if (!dirty) ImGui::EndDisabled();
      ImGui::SameLine();
      if (!dirty && live.overlayRunning) {
        ImGui::TextDisabled(Tr("Restart the overlay to apply."));
      }

      ImGui::Dummy(ImVec2(0.0f, S(16.0f)));
      if (!ImGui::TreeNode(Tr("Every individual setting"))) {
        Hint("Nothing in here is needed for normal use.");
      } else {
      SectionHeading("Pipeline");
      Hint("These are written to sidecar.toml, and the neural ones are projected "
           "into ReShade.ini where the add-on reads them. The add-on reads that "
           "file once, when it loads, so a change reaches the picture at the "
           "overlay's next start -- not while it runs.");
      ImGui::Dummy(ImVec2(0.0f, S(8.0f)));

      {
        int passIndex = config.neuralPass == "reshade" ? 1 : 0;
        const char* passes[] = {Tr("passthrough -- capture and present, untouched"),
                                Tr("reshade -- DLSS 5 neural rendering")};
        ImGui::SetNextItemWidth(S(440.0f));
        if (ImGui::Combo(Tr("Neural pass"), &passIndex, passes, 2)) {
          config.neuralPass = passIndex == 1 ? "reshade" : "passthrough";
          dirty = true;
        }
        Hint("Passthrough is the honest A/B baseline: the same capture and "
             "present path with no neural work in it at all.");
      }

      {
        // Built from the enum's own table so a preset added to NgxSession
        // appears here without anyone remembering to update a second list.
        std::vector<const char*> names;
        std::vector<const char*> descriptions;
        int current = 0;
        for (const auto* choice = DlssPresetChoices(); choice->name; ++choice) {
          if (config.dlssPreset == choice->name) current = static_cast<int>(names.size());
          names.push_back(choice->name);
          descriptions.push_back(choice->description);
        }
        ImGui::SetNextItemWidth(S(440.0f));
        if (ImGui::Combo(Tr("DLSS preset"), &current, names.data(),
                         static_cast<int>(names.size()))) {
          config.dlssPreset = names[static_cast<size_t>(current)];
          dirty = true;
        }
        Hint(descriptions[static_cast<size_t>(current)]);
      }

      {
        const char* grids[] = {Tr("1 -- finest, most expensive"), "2", Tr("4 -- default")};
        int index = config.flowGridSize == 1 ? 0 : config.flowGridSize == 2 ? 1 : 2;
        ImGui::SetNextItemWidth(S(440.0f));
        if (ImGui::Combo(Tr("Optical flow grid"), &index, grids, 3)) {
          config.flowGridSize = index == 0 ? 1u : index == 1 ? 2u : 4u;
          dirty = true;
        }
        Hint("Pixels per estimated motion vector. Finer costs more and is not "
             "obviously better: the vectors are estimated from colour, so a "
             "finer grid can sharpen the estimate or the error equally.");
      }

      ImGui::SetNextItemWidth(S(440.0f));
      if (ImGui::SliderFloat(Tr("Synthetic depth"), &config.syntheticDepth, 0.0f, 1.0f,
                             "%.2f")) {
        dirty = true;
      }
      Hint("There is no real depth buffer to capture out of the composited "
           "frame, so a constant plane is bound instead. It costs temporal "
           "stability under motion rather than preventing NR from running; this "
           "dial is worth a try if motion smears.");

      ImGui::Dummy(ImVec2(0.0f, S(16.0f)));
      SectionHeading("Neural rendering strength");
      Hint("Passed straight through to the RenoDX add-on. The add-on owns what "
           "these mean; we only carry them.");
      ImGui::Dummy(ImVec2(0.0f, S(8.0f)));

      auto& n = config.neural;
      ImGui::SetNextItemWidth(S(440.0f));
      if (ImGui::SliderFloat(Tr("Intensity"), &n.intensity, 0.0f, 1.0f, "%.2f")) dirty = true;
      Hint("How much of the neural result is mixed in. Start here.");

      ImGui::SetNextItemWidth(S(440.0f));
      if (ImGui::SliderInt(Tr("Add-on preset"), &n.preset, 0, 3)) dirty = true;
      ImGui::SetNextItemWidth(S(440.0f));
      if (ImGui::SliderInt(Tr("Style"), &n.style, 0, 3)) dirty = true;

      ImGui::SetNextItemWidth(S(440.0f));
      if (ImGui::SliderFloat(Tr("Colour strength"), &n.colorStrength, 0.0f, 1.0f, "%.2f")) {
        dirty = true;
      }
      Hint("At zero the add-on's colour handling collapses to black, so this is "
           "not a subtle dial.");

      ImGui::SetNextItemWidth(S(440.0f));
      if (ImGui::SliderFloat(Tr("HDR transfer strength"), &n.transferStrength, 0.0f, 1.0f,
                             "%.2f")) {
        dirty = true;
      }
      ImGui::SetNextItemWidth(S(440.0f));
      if (ImGui::SliderFloat(Tr("Paper-white scale"), &n.paperWhiteScale, 0.0f, 4.0f, "%.2f")) {
        dirty = true;
      }
      Hint("Both only matter on an HDR display. The sidecar captures SDR today, "
           "so leave them at 1.00 unless you are experimenting.");

      ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
      if (ImGui::TreeNode(Tr("Advanced"))) {
        Hint("These three have no documented default, so they are written only "
             "once you move them. Clearing the box returns them to untouched, "
             "which leaves whatever the add-on does by itself.");
        // The label doubles as the widget id, so it is translated at the point
        // it is drawn and the id stays the English one.
        const auto optional = [&](const char* label, const char* explanation,
                                  float& value) {
          bool set = value >= 0.0f;
          ImGui::PushID(label);
          if (ImGui::Checkbox("##set", &set)) {
            value = set ? 1.0f : -1.0f;
            dirty = true;
          }
          ImGui::SameLine();
          if (!set) ImGui::BeginDisabled();
          float shown = set ? value : 1.0f;
          ImGui::SetNextItemWidth(S(390.0f));
          if (ImGui::SliderFloat(Tr(label), &shown, 0.0f, 2.0f, "%.2f") && set) {
            value = shown;
            dirty = true;
          }
          if (!set) ImGui::EndDisabled();
          Hint(explanation);
          ImGui::PopID();
        };
        optional("Local structure",
                 "How hard the model works on fine surface detail -- stonework, "
                 "cloth weave, wood grain. Raising it sharpens texture and also "
                 "sharpens whatever the model got wrong about it.",
                 n.localStructure);
        optional("Local tone",
                 "Local contrast: how far the model pushes light against dark "
                 "within a small area. This is the dial most likely to change "
                 "the mood of a scene rather than its detail.",
                 n.localTone);
        optional("Skin structure",
                 "Faces and skin specifically. The one people reach for after "
                 "watching a comparison video, and the one most likely to look "
                 "wrong when overdone -- skin is where anybody spots a mistake.",
                 n.skinStructure);

        ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
        if (ImGui::Checkbox(Tr("Enable the add-on's upscaling (work in progress)"),
                            &n.upscaling)) {
          dirty = true;
        }
        Hint("The add-on marks this unfinished, and the sidecar feeds it a "
             "native-resolution DLAA contract, so it usually reports that it "
             "fell back to native. Off is the tested path.");

        ImGui::SetNextItemWidth(S(390.0f));
        if (ImGui::SliderInt(Tr("Hook mode"), &n.enableHooks, 0, 2)) dirty = true;
        Hint("0 turns neural rendering off entirely, 1 adds Streamline hooks, 2 "
             "is NGX only. Two is what this sidecar wants: it makes the NGX "
             "calls itself and there is no Streamline in the process.");
        ImGui::TreePop();
      }

      ImGui::Dummy(ImVec2(0.0f, S(16.0f)));
      SectionHeading("Overlay");
      if (ImGui::Checkbox(Tr("Show the HUD"), &config.showHud)) dirty = true;
      Hint("The strip of numbers the overlay draws in its own top-left corner: "
           "frame rate, latency, which pass is running and how many frames were "
           "dropped. It is drawn by the overlay, not by the game, so it never "
           "reaches a screenshot the game takes.");
      if (ImGui::Checkbox(Tr("Show the overlay on start"), &config.showOverlay)) dirty = true;
      Hint("Whether the overlay covers the game the moment it launches. Off "
           "starts it hidden, which is the honest way to look at the untouched "
           "frame first and then bring the neural pass in over the top of it.");

      ImGui::Dummy(ImVec2(0.0f, S(12.0f)));
      if (ImGui::Button(Tr("Reset to defaults"), ImVec2(S(170.0f), S(30.0f)))) {
        const auto mask = config.uiMaskRects;   // calibration is not a setting
        config = Config{};
        config.uiMaskRects = mask;
        dirty = true;
      }
      ImGui::TreePop();
      }
    }

    if (section == Section::Log) {
      SectionHeading("Log");
      const std::string lastError = GlobalLog().LastError();
      if (!lastError.empty()) {
        ImGui::TextColored(Rgb(g_colors.fail), "%s", Tr("Last error"));
        ImGui::TextWrapped("%s", lastError.c_str());
        ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
      }

      // Re-read a few times a second rather than every frame. The file is
      // small, but reading it sixty times a second to show the same bytes is
      // work nobody asked for.
      const double now = ImGui::GetTime();
      if (now - overlayLogReadAt > 0.5) {
        overlayLog = TailOfFile(sidecarDir / "sidecar.log", 400);
        overlayLogReadAt = now;
      }

      // Two panes, because there are two programs. The overlay's is first and
      // larger: when something has gone wrong, that is the one with the answer
      // in it, and the manager's own half of the story is always the same two
      // lines about having asked for a launch.
      const float available = ImGui::GetContentRegionAvail().y - S(64.0f);
      const float overlayHeight = std::max(available * 0.6f, S(120.0f));

      ImGui::TextColored(Rgb(g_colors.accent), "%s", Tr("The overlay (sidecar.log)"));
      ImGui::BeginChild("overlaylog", ImVec2(0.0f, overlayHeight), ImGuiChildFlags_Border);
      if (g_fonts.mono) ImGui::PushFont(g_fonts.mono);
      if (overlayLog.empty()) {
        ImGui::TextDisabled("%s", Tr("The overlay has not written anything yet."));
      }
      for (const auto& line : overlayLog) {
        const bool bad = line.find("ERROR") != std::string::npos;
        if (bad) {
          ImGui::TextColored(Rgb(g_colors.fail), "%s", line.c_str());
        } else {
          ImGui::TextUnformatted(line.c_str());
        }
      }
      if (g_fonts.mono) ImGui::PopFont();
      // Follow the tail while it grows, but only while the operator has not
      // scrolled up to read something.
      if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - S(4.0f)) {
        ImGui::SetScrollHereY(1.0f);
      }
      ImGui::EndChild();

      ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
      ImGui::TextColored(Rgb(g_colors.accent), "%s", Tr("This manager"));
      ImGui::BeginChild("logscroll", ImVec2(0.0f, S(-28.0f)), ImGuiChildFlags_Border);
      if (g_fonts.mono) ImGui::PushFont(g_fonts.mono);
      for (const auto& line : GlobalLog().Recent()) {
        ImGui::TextUnformatted(line.c_str());
      }
      if (g_fonts.mono) ImGui::PopFont();
      ImGui::EndChild();
      const uint64_t dropped = GlobalLog().Dropped();
      if (dropped > 0) {
        ImGui::TextDisabled(Tr("%llu earlier line(s) dropped"),
                            static_cast<unsigned long long>(dropped));
      }
    }

    ImGui::EndChild();
    ImGui::Unindent(kPad);
    if (!noticeAcknowledged) ImGui::EndDisabled();
    ImGui::End();

    if (!noticeAcknowledged) {
      ImGui::OpenPopup(kFirstRunTitle);
      const ImVec2 center = viewport->GetCenter();
      ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(S(0.5f), S(0.5f)));
      ImGui::SetNextWindowSize(ImVec2(S(600.0f), 0.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(24.0f), S(20.0f)));
      // No title bar: the heading below is the title, and ImGui's own bar would
      // print the same words a second time in a different face.
      if (ImGui::BeginPopupModal(kFirstRunTitle, nullptr,
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                     ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoTitleBar)) {
        if (g_fonts.heading) ImGui::PushFont(g_fonts.heading);
        ImGui::TextColored(Rgb(g_colors.goldBright), "%s", Tr(kFirstRunTitle));
        if (g_fonts.heading) ImGui::PopFont();
        GoldRule(0.45f, 10.0f);
        ImGui::TextWrapped("%s", Tr(kFirstRunBody));
        ImGui::Dummy(ImVec2(0.0f, S(12.0f)));
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(g_colors.goldBright));
        if (ImGui::Button(Tr("I understand"), ImVec2(S(170.0f), S(34.0f)))) {
          noticeAcknowledged = true;
          // The config file's existence is what records the acknowledgement, so
          // write it here. It lives beside the manager, never in WoW's
          // directory (I5).
          SaveConfig(configPath, config);
          ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
        ImGui::EndPopup();
      }
      ImGui::PopStyleVar();
    }

    ImGui::Render();
    const float clear[4] = {0.043f, 0.047f, 0.071f, 1.0f};
    ID3D11RenderTargetView* rtv = g_backBufferRtv.Get();
    g_context->OMSetRenderTargets(1, &rtv, nullptr);
    g_context->ClearRenderTargetView(rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_swapChain->Present(1, 0);
  }

  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  return 0;
}
