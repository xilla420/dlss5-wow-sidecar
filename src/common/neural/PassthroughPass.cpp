#include "neural/PassthroughPass.h"

namespace sidecar {

std::unique_ptr<PassthroughPass> PassthroughPass::Create() {
  return std::make_unique<PassthroughPass>();
}

bool PassthroughPass::Evaluate(ID3D12GraphicsCommandList* cl,
                               ID3D12Resource* color,
                               ID3D12Resource* /*motion*/,
                               ID3D12Resource* /*depth*/,
                               ID3D12Resource* out) {
  if (!cl || !color || !out) return false;
  cl->CopyResource(out, color);
  return true;
}

}  // namespace sidecar
