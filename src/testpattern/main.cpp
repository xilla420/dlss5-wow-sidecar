// A borderless-windowed D3D11 app that renders the deterministic pattern and
// reports its current frame index in the window title. It stands in for WoW so
// the capture and present path can be verified without the game.
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstdlib>
#include <vector>
#include <string>
#include "Pattern.h"

using Microsoft::WRL::ComPtr;
using namespace sidecar::testpattern;

namespace {

// 720p by default, which is what the capture tests assume. An optional
// "<width> <height>" on the command line exists so a performance measurement
// can be taken at the resolution someone actually plays at -- the neural pass
// costs what it costs per pixel, and a 720p reading says nothing useful about
// a 1440p frame budget.
constexpr uint32_t kDefaultW = 1280, kDefaultH = 720;
// Sync interval for Present. Vsync by default, because the capture tests want a
// steady, predictable source. A third argument of 0 unlocks it, which is the
// only way to tell whether a frame-rate ceiling measured downstream belongs to
// the sidecar or to this harness -- a vsync-locked stand-in caps the whole
// chain and makes the sidecar look slow.
UINT g_syncInterval = 1;
UINT g_frame = 0;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
  uint32_t kW = kDefaultW, kH = kDefaultH;
  {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 3) {
      const long width = std::wcstol(argv[1], nullptr, 10);
      const long height = std::wcstol(argv[2], nullptr, 10);
      // Anything outside this is a typo rather than an intention, and a
      // zero-sized swapchain fails in a much less obvious place.
      if (width >= 256 && width <= 7680 && height >= 256 && height <= 4320) {
        kW = static_cast<uint32_t>(width);
        kH = static_cast<uint32_t>(height);
      }
    }
    if (argc >= 4) {
      const long sync = std::wcstol(argv[3], nullptr, 10);
      if (sync >= 0 && sync <= 4) g_syncInterval = static_cast<UINT>(sync);
    }
    if (argv) LocalFree(argv);
  }

  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.lpszClassName = L"SidecarTestPattern";
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  RegisterClassExW(&wc);

  RECT r{0, 0, static_cast<LONG>(kW), static_cast<LONG>(kH)};
  AdjustWindowRect(&r, WS_POPUP, FALSE);
  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Sidecar Test Pattern",
                              WS_POPUP | WS_VISIBLE, 100, 100,
                              r.right - r.left, r.bottom - r.top,
                              nullptr, nullptr, inst, nullptr);

  DXGI_SWAP_CHAIN_DESC1 scd{};
  scd.Width = kW; scd.Height = kH;
  scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  scd.SampleDesc.Count = 1;
  scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  scd.BufferCount = 2;
  scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

  ComPtr<ID3D11Device> dev;
  ComPtr<ID3D11DeviceContext> ctx;
  ComPtr<IDXGISwapChain1> sc;
  D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                    D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                    D3D11_SDK_VERSION, &dev, nullptr, &ctx);
  ComPtr<IDXGIDevice> dxgiDev;
  dev.As(&dxgiDev);
  ComPtr<IDXGIAdapter> adapter;
  dxgiDev->GetAdapter(&adapter);
  ComPtr<IDXGIFactory2> factory;
  adapter->GetParent(IID_PPV_ARGS(&factory));
  factory->CreateSwapChainForHwnd(dev.Get(), hwnd, &scd, nullptr, nullptr, &sc);

  // A CPU-side staging texture is fine here: correctness matters, speed does not.
  D3D11_TEXTURE2D_DESC td{};
  td.Width = kW; td.Height = kH; td.MipLevels = 1; td.ArraySize = 1;
  td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DYNAMIC;
  td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  ComPtr<ID3D11Texture2D> staging;
  dev->CreateTexture2D(&td, nullptr, &staging);

  ShowWindow(hwnd, show);

  MSG msg{};
  while (msg.message != WM_QUIT) {
    if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
      continue;
    }

    D3D11_MAPPED_SUBRESOURCE m{};
    ctx->Map(staging.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
    for (uint32_t y = 0; y < kH; ++y) {
      auto* row = reinterpret_cast<Bgra*>(static_cast<uint8_t*>(m.pData) + y * m.RowPitch);
      for (uint32_t x = 0; x < kW; ++x) row[x] = PixelAt(x, y, kW, kH, g_frame);
    }
    ctx->Unmap(staging.Get(), 0);

    ComPtr<ID3D11Texture2D> back;
    sc->GetBuffer(0, IID_PPV_ARGS(&back));
    ctx->CopyResource(back.Get(), staging.Get());
    sc->Present(g_syncInterval, 0);

    std::wstring title = L"Sidecar Test Pattern - frame " + std::to_wstring(g_frame);
    SetWindowTextW(hwnd, title.c_str());
    ++g_frame;
  }
  return 0;
}
