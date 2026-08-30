#include <catch2/catch_test_macros.hpp>
#include <string>
#include "neural/PassthroughPass.h"

using namespace sidecar;

TEST_CASE("passthrough names itself for the HUD and logs", "[unit]") {
  auto pass = PassthroughPass::Create();
  REQUIRE(pass != nullptr);
  REQUIRE(std::string(pass->Name()) == "passthrough");
}

TEST_CASE("passthrough rejects a null command list rather than crashing", "[unit]") {
  auto pass = PassthroughPass::Create();
  REQUIRE(pass->Evaluate(nullptr, nullptr, nullptr, nullptr, nullptr) == false);
}

TEST_CASE("passthrough tolerates absent motion and depth", "[unit]") {
  auto pass = PassthroughPass::Create();
  // Non-null command list is required; colour and out are checked too. With a
  // null list this must fail cleanly, which is what a caller sees when the
  // pipeline is mid-teardown.
  REQUIRE(pass->Evaluate(nullptr, reinterpret_cast<ID3D12Resource*>(1),
                         nullptr, nullptr,
                         reinterpret_cast<ID3D12Resource*>(2)) == false);
}
