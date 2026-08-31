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

  // The resource state `out` must be in when Evaluate is called.
  //
  // Passes disagree about this and the caller cannot guess: PassthroughPass
  // writes with CopyResource and needs COPY_DEST, while anything driving NGX
  // writes through an unordered-access view. Getting it wrong is a debug-layer
  // error at best and a device removal under load at worst, so the pass states
  // its own requirement and the pipeline brackets accordingly.
  virtual D3D12_RESOURCE_STATES OutputState() const {
    return D3D12_RESOURCE_STATE_COPY_DEST;
  }

  // The format this pass wants to write, or UNKNOWN for "whatever the caller's
  // work target already is".
  //
  // A pass that names a format is asking the pipeline for its own intermediate
  // target in that format, which the blend stage then resolves down to the
  // presentable one. NGX wants explicitly typed float colour on both ends --
  // the whole reason FormatNormalize exists (spec §8) -- while the overlay
  // presents BGRA8, and something has to bridge the two.
  virtual DXGI_FORMAT OutputFormat() const { return DXGI_FORMAT_UNKNOWN; }

  // Shown in the HUD so it is always obvious which pass is live.
  virtual const char* Name() const = 0;
};

}  // namespace sidecar
