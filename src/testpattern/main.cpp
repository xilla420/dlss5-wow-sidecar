// A borderless-windowed D3D11 app that renders the deterministic pattern and
// reports its current frame index in the window title. It stands in for WoW so
// the capture and present path can be verified without the game.
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include "Pattern.h"

using Microsoft::WRL::ComPtr;
using namespace sidecar::testpattern;

namespace {

constexpr uint32_t kW = 1280, kH = 720;
UINT g_frame = 0;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
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
    sc->Present(1, 0);

    std::wstring title = L"Sidecar Test Pattern - frame " + std::to_wstring(g_frame);
    SetWindowTextW(hwnd, title.c_str());
    ++g_frame;
  }
  return 0;
}
