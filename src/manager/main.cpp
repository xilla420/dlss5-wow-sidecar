// wowsidecar-manager.exe: the dependency board.
//
// The manager only ever reports and launches. It never modifies a WoW
// installation, and the probes it runs are the same predicates the unit tests
// pin down (I7, I8, I9).
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "core/Config.h"
#include "manager/Probes.h"
#include "manager/Theme.h"

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;
using namespace sidecar;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

constexpr int kWindowWidth = 1040;
constexpr int kWindowHeight = 720;

ComPtr<ID3D11Device> g_device;
ComPtr<ID3D11DeviceContext> g_context;
ComPtr<IDXGISwapChain1> g_swapChain;
ComPtr<ID3D11RenderTargetView> g_backBufferRtv;

// The notice a first-time operator has to read before anything is enabled.
// Wording is fixed by the spec; it is the honest version of the safety claim,
// not a disclaimer.
constexpr const char* kFirstRunTitle = "Before you use this";
constexpr const char* kFirstRunBody =
    "This sidecar never loads code into Wow.exe. That is what makes it safe, "
    "and it is checked automatically every time it is built.\n\n"
    "ReShade placed next to Wow.exe is a different thing entirely, and "
    "Blizzard bans accounts for it. This tool will refuse to install there and "
    "will refuse to run if it finds an injector in your WoW folder.\n\n"
    "No third-party tool can promise you will never be banned. What this one "
    "can promise is that it does not do any of the things Blizzard bans people "
    "for.";

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
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wp, lp);
  }
}

fs::path ExecutableDirectory() {
  wchar_t buffer[MAX_PATH]{};
  GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  return fs::path(buffer).parent_path();
}

const char* StateLabel(ProbeState state) {
  switch (state) {
    case ProbeState::Ok:   return "OK";
    case ProbeState::Warn: return "WARN";
    default:               return "FAIL";
  }
}

ImVec4 StateColor(ProbeState state, const ThemeColors& colors) {
  const unsigned int hex = state == ProbeState::Ok     ? colors.ok
                           : state == ProbeState::Warn ? colors.warn
                                                       : colors.fail;
  return ImVec4(((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f,
                (hex & 0xFF) / 255.0f, 1.0f);
}

// Writes a commented default config. Its presence is also what records that
// the first-run notice has been read.
void WriteDefaultConfig(const fs::path& path) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) return;
  file << "# DLSS 5 Sidecar configuration.\n"
          "# Unknown keys and bad values warn and fall back; they never stop\n"
          "# the overlay from starting.\n\n"
          "show_hud = true\n"
          "show_overlay = true\n"
          "flow_grid_size = 4      # 1, 2 or 4 pixels per flow vector\n"
          "neural_pass = \"passthrough\"\n\n"
          "# Screen-space rectangles the neural pass should leave alone.\n"
          "# [[ui_mask]]\n"
          "# left = 0\n"
          "# top = 900\n"
          "# right = 1920\n"
          "# bottom = 1080\n";
}

// Blocks launching whenever a probe says the system is in a state the design
// refuses to run in.
bool AnyBlockingFailure(const std::vector<ProbeResult>& results) {
  for (const auto& r : results) {
    if (r.state == ProbeState::Fail) return true;
  }
  return false;
}

void DrawBoard(const std::vector<ProbeResult>& results, const ThemeColors& colors) {
  if (!ImGui::BeginTable("probes", 3,
                         ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                             ImGuiTableFlags_SizingStretchProp)) {
    return;
  }
  ImGui::TableSetupColumn("state", ImGuiTableColumnFlags_WidthFixed, 64.0f);
  ImGui::TableSetupColumn("check", ImGuiTableColumnFlags_WidthFixed, 260.0f);
  ImGui::TableSetupColumn("detail", ImGuiTableColumnFlags_WidthStretch);

  for (const auto& r : results) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextColored(StateColor(r.state, colors), "%s", StateLabel(r.state));

    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(r.title.c_str());

    ImGui::TableSetColumnIndex(2);
    ImGui::TextWrapped("%s", r.detail.c_str());
    if (r.state != ProbeState::Ok && !r.remedy.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::TextWrapped("%s", r.remedy.c_str());
      ImGui::PopStyleColor();
    }
  }
  ImGui::EndTable();
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
  WNDCLASSEXW wc{sizeof(wc)};
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = L"SidecarManager";
  RegisterClassExW(&wc);

  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"DLSS 5 Sidecar - Manager",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              kWindowWidth, kWindowHeight, nullptr, nullptr,
                              inst, nullptr);
  if (!hwnd || !CreateDeviceAndSwapChain(hwnd)) {
    MessageBoxW(nullptr, L"Could not create a Direct3D 11 device.",
                L"DLSS 5 Sidecar", MB_ICONERROR);
    return 1;
  }
  ShowWindow(hwnd, show);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;   // no state file next to the binary
  const bool dark = true;
  ApplySidecarTheme(dark);
  const ThemeColors colors = CurrentThemeColors(dark);
  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplDX11_Init(g_device.Get(), g_context.Get());

  const fs::path sidecarDir = ExecutableDirectory();
  const fs::path configPath = sidecarDir / "sidecar.toml";

  // No config file means nobody has been shown the notice yet.
  std::error_code ec;
  bool noticeAcknowledged = fs::exists(configPath, ec) && !ec;

  std::vector<std::string> warnings;
  Config config;
  if (auto loaded = LoadConfig(configPath, warnings)) config = *loaded;

  std::string wowDirUtf8;
  wowDirUtf8.resize(512);
  auto results = RunAllProbes(sidecarDir, fs::path{});

  MSG msg{};
  while (msg.message != WM_QUIT) {
    if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
      continue;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("board", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::TextUnformatted("Dependency board");
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped(
        "Nothing here opens, reads, writes or hooks the game process. The "
        "import table of every binary is checked against that claim at build "
        "time.");
    ImGui::PopStyleColor();
    ImGui::Separator();

    if (!noticeAcknowledged) ImGui::BeginDisabled();

    ImGui::TextUnformatted("WoW folder (used only to scan filenames for injectors)");
    ImGui::SetNextItemWidth(-160.0f);
    ImGui::InputText("##wowdir", wowDirUtf8.data(), wowDirUtf8.size());
    ImGui::SameLine();
    if (ImGui::Button("Re-run probes", ImVec2(150.0f, 0.0f))) {
      results = RunAllProbes(sidecarDir, fs::path(wowDirUtf8.c_str()));
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    DrawBoard(results, colors);
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    const bool blocked = AnyBlockingFailure(results);
    if (blocked) ImGui::BeginDisabled();
    if (ImGui::Button("Launch overlay", ImVec2(190.0f, 0.0f))) {
      const fs::path runtime = sidecarDir / "wowsidecar.exe";
      ShellExecuteW(nullptr, L"open", runtime.c_str(), nullptr,
                    sidecarDir.c_str(), SW_SHOWNORMAL);
    }
    if (blocked) ImGui::EndDisabled();
    if (blocked) {
      ImGui::SameLine();
      ImGui::TextColored(StateColor(ProbeState::Fail, colors),
                         "Resolve the failing checks above first.");
    }

    if (!noticeAcknowledged) ImGui::EndDisabled();

    ImGui::End();

    if (!noticeAcknowledged) {
      ImGui::OpenPopup(kFirstRunTitle);
      const ImVec2 center = viewport->GetCenter();
      ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f));
      if (ImGui::BeginPopupModal(kFirstRunTitle, nullptr,
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                     ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextWrapped("%s", kFirstRunBody);
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        if (ImGui::Button("I understand", ImVec2(160.0f, 0.0f))) {
          noticeAcknowledged = true;
          // The config file's existence is what records the acknowledgement,
          // so write it here. It lives beside the manager, never in WoW's
          // directory (I5).
          WriteDefaultConfig(configPath);
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }
    }

    ImGui::Render();
    const float clear[4] = {0.08f, 0.09f, 0.10f, 1.0f};
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
