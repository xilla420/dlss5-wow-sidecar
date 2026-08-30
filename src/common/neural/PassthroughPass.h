#pragma once
#include <memory>
#include "neural/INeuralPass.h"

namespace sidecar {

// Copies colour to out. Used to validate the real-time pipeline before any
// unredistributable binary is involved, and as the permanent fallback when the
// neural runtime is missing or fails to initialise (spec section 11).
class PassthroughPass final : public INeuralPass {
 public:
  static std::unique_ptr<PassthroughPass> Create();

  bool Evaluate(ID3D12GraphicsCommandList* cl,
                ID3D12Resource* color,
                ID3D12Resource* motion,
                ID3D12Resource* depth,
                ID3D12Resource* out) override;

  const char* Name() const override { return "passthrough"; }
};

}  // namespace sidecar
