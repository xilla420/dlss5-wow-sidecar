#pragma once
#include <d3d12.h>

namespace sidecar {

// How the neural pass is invoked. Three implementations exist across the
// project: PassthroughPass (here), ReshadeHostedPass and DirectNgxPass (both
// deferred until after the M1 gate).
//
// motion and depth may be null. An implementation that requires them returns
// false rather than dereferencing, so the pipeline can fall back cleanly.
class INeuralPass {
 public:
  virtual ~INeuralPass() = default;

  virtual bool Evaluate(ID3D12GraphicsCommandList* cl,
                        ID3D12Resource* color,
                        ID3D12Resource* motion,
                        ID3D12Resource* depth,
                        ID3D12Resource* out) = 0;

  // Shown in the HUD so it is always obvious which pass is live.
  virtual const char* Name() const = 0;
};

}  // namespace sidecar
