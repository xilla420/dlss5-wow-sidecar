#include "gpu/DeviceBridge.h"

using Microsoft::WRL::ComPtr;

namespace sidecar {
namespace {

ComPtr<IDXGIAdapter1> FindAdapterByLuid(LUID luid) {
  ComPtr<IDXGIFactory4> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return nullptr;
  ComPtr<IDXGIAdapter1> adapter;
  if (FAILED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter)))) return nullptr;
  return adapter;
}

}  // namespace

std::unique_ptr<DeviceBridge> DeviceBridge::Create(LUID adapterLuid,
                                                   uint32_t width, uint32_t height) {
  auto adapter = FindAdapterByLuid(adapterLuid);
  if (!adapter) return nullptr;

  std::unique_ptr<DeviceBridge> b(new DeviceBridge());
  b->width_ = width;
  b->height_ = height;

  // Both devices are created on the same adapter so shared handles are valid.
  ComPtr<ID3D11Device> dev11;
  ComPtr<ID3D11DeviceContext> ctx11;
  const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
                               nullptr, 0, D3D11_SDK_VERSION, &dev11, nullptr, &ctx11))) {
    return nullptr;
  }
  if (FAILED(dev11.As(&b->d3d11_)) || FAILED(ctx11.As(&b->d3d11Ctx_))) return nullptr;

  if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&b->d3d12_)))) {
    return nullptr;
  }
  D3D12_COMMAND_QUEUE_DESC qd{};
  qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (FAILED(b->d3d12_->CreateCommandQueue(&qd, IID_PPV_ARGS(&b->queue_)))) return nullptr;

  // Shared fence: signalled on the D3D11 context, waited on the D3D12 queue.
  if (FAILED(b->d3d11_->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&b->fence11_)))) {
    return nullptr;
  }
  if (FAILED(b->fence11_->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &b->fenceHandle_))) {
    return nullptr;
  }
  if (FAILED(b->d3d12_->OpenSharedHandle(b->fenceHandle_, IID_PPV_ARGS(&b->fence12_)))) {
    return nullptr;
  }

  // Shared texture ring.
  D3D11_TEXTURE2D_DESC td{};
  td.Width = width;
  td.Height = height;
  td.MipLevels = 1;
  td.ArraySize = 1;
  td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DEFAULT;
  td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;

  for (auto& slot : b->ring_) {
    if (FAILED(b->d3d11_->CreateTexture2D(&td, nullptr, &slot.tex11))) return nullptr;
    ComPtr<IDXGIResource1> dxgiRes;
    if (FAILED(slot.tex11.As(&dxgiRes))) return nullptr;
    if (FAILED(dxgiRes->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ |
                                                    DXGI_SHARED_RESOURCE_WRITE,
                                           nullptr, &slot.sharedHandle))) {
      return nullptr;
    }
    if (FAILED(b->d3d12_->OpenSharedHandle(slot.sharedHandle, IID_PPV_ARGS(&slot.tex12)))) {
      return nullptr;
    }
  }
  return b;
}

DeviceBridge::~DeviceBridge() {
  for (auto& slot : ring_) {
    if (slot.sharedHandle) CloseHandle(slot.sharedHandle);
  }
  if (fenceHandle_) CloseHandle(fenceHandle_);
}

bool DeviceBridge::Publish(ID3D11Texture2D* src) {
  Slot& slot = ring_[writeIndex_];
  d3d11Ctx_->CopyResource(slot.tex11.Get(), src);

  slot.fenceValue = nextFenceValue_++;
  d3d11Ctx_->Signal(fence11_.Get(), slot.fenceValue);
  d3d11Ctx_->Flush();

  // Latest-wins. exchange() tells us whether a frame was still pending.
  const int32_t previous = published_.exchange(static_cast<int32_t>(writeIndex_),
                                               std::memory_order_release);
  writeIndex_ = (writeIndex_ + 1) % kRingDepth;
  return previous != -1;
}

std::optional<BridgeFrame> DeviceBridge::AcquireLatest() {
  const int32_t index = published_.exchange(-1, std::memory_order_acquire);
  if (index < 0) return std::nullopt;

  const Slot& slot = ring_[static_cast<size_t>(index)];
  BridgeFrame f;
  f.texture = slot.tex12.Get();
  f.fenceValue = slot.fenceValue;
  f.index = static_cast<uint32_t>(index);
  return f;
}

}  // namespace sidecar
