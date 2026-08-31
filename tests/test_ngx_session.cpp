#include <catch2/catch_test_macros.hpp>
#include <d3d12.h>
#include <wrl/client.h>

#include <filesystem>

#include "core/GpuProfile.h"
#include "gpu/DeviceBridge.h"
#include "neural/NgxSession.h"

using Microsoft::WRL::ComPtr;
using namespace sidecar;

// NgxSession is the first thing in this project that brings up a vendor runtime
// on the render device, and it is built behind a compile-time gate, so the
// no-SDK build never executes a line of it. These tests exist to make sure the
// gated-in version is not shipped having never run: they assert the contract
// that holds whether or not a DLSS runtime is actually installed.

TEST_CASE("NgxSession refuses a null device", "[unit]") {
  REQUIRE(NgxSession::Create(nullptr, ".") == nullptr);
}

TEST_CASE("NgxSession always explains itself when unavailable", "[device]") {
  const auto gpu = DetectPrimaryGpu();
  if (!gpu) { SUCCEED("no NVIDIA adapter"); return; }
  auto bridge = DeviceBridge::Create(gpu->luid, 256, 256);
  REQUIRE(bridge != nullptr);

  // No runtimes are placed next to the test binary, so on a machine without
  // them this exercises the failure path -- which is the path that matters,
  // because it is what every user without the optional runtime will hit.
  auto session = NgxSession::Create(bridge->D3d12(),
                                    std::filesystem::current_path());
  REQUIRE(session != nullptr);

  // The invariant: never silently unavailable. Either it came up, or it says
  // why in terms an operator could act on.
  if (!session->Available() || !session->DlssSupported()) {
    INFO("reason: " << session->UnavailableReason());
    CHECK(session->UnavailableReason().empty() == false);
  }
}

TEST_CASE("NgxSession without a feature refuses to evaluate", "[device]") {
  const auto gpu = DetectPrimaryGpu();
  if (!gpu) { SUCCEED("no NVIDIA adapter"); return; }
  auto bridge = DeviceBridge::Create(gpu->luid, 256, 256);
  REQUIRE(bridge != nullptr);
  auto session = NgxSession::Create(bridge->D3d12(),
                                    std::filesystem::current_path());
  REQUIRE(session != nullptr);
  CHECK(session->HasFeature() == false);

  ComPtr<ID3D12CommandAllocator> alloc;
  ComPtr<ID3D12GraphicsCommandList> cl;
  REQUIRE(SUCCEEDED(bridge->D3d12()->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))));
  REQUIRE(SUCCEEDED(bridge->D3d12()->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cl))));

  // Evaluating with no feature, and with null resources, must return false
  // rather than reach NGX with a null it would fault on.
  DlssEvalDesc eval{};
  CHECK(session->Evaluate(cl.Get(), eval) == false);
  CHECK(session->Evaluate(nullptr, eval) == false);

  cl->Close();
}

TEST_CASE("NgxSession rejects a degenerate feature description", "[device]") {
  const auto gpu = DetectPrimaryGpu();
  if (!gpu) { SUCCEED("no NVIDIA adapter"); return; }
  auto bridge = DeviceBridge::Create(gpu->luid, 256, 256);
  REQUIRE(bridge != nullptr);
  auto session = NgxSession::Create(bridge->D3d12(),
                                    std::filesystem::current_path());
  REQUIRE(session != nullptr);

  ComPtr<ID3D12CommandAllocator> alloc;
  ComPtr<ID3D12GraphicsCommandList> cl;
  REQUIRE(SUCCEEDED(bridge->D3d12()->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))));
  REQUIRE(SUCCEEDED(bridge->D3d12()->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cl))));

  DlssFeatureDesc desc{};  // all zero
  CHECK(session->CreateDlssFeature(cl.Get(), desc) == false);
  CHECK(session->HasFeature() == false);

  cl->Close();
}

// Teardown is where the earlier vendor-runtime integration went wrong twice
// over: NvofaFlow crashed on exit until it stopped calling into a removed
// device and stopped unloading its DLL. Constructing and destroying repeatedly
// is the cheapest way to catch the same class of mistake here.
TEST_CASE("NgxSession can be created and destroyed repeatedly", "[device]") {
  const auto gpu = DetectPrimaryGpu();
  if (!gpu) { SUCCEED("no NVIDIA adapter"); return; }
  auto bridge = DeviceBridge::Create(gpu->luid, 256, 256);
  REQUIRE(bridge != nullptr);
  for (int i = 0; i < 3; ++i) {
    INFO("iteration " << i);
    auto session = NgxSession::Create(bridge->D3d12(),
                                      std::filesystem::current_path());
    REQUIRE(session != nullptr);
    session->ReleaseFeature();  // idempotent with no feature
  }
  SUCCEED();
}
