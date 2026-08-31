// Blends the neural output back against the original captured frame, keeping
// the original wherever the mask says "interface".
//
// The neural pass re-lights and re-materialises. That is wanted on the world
// and emphatically not wanted on action bars, unit frames, chat or the minimap,
// which are authored 2D art that any re-lighting can only damage. The spec calls
// this the largest quality risk in the project.
//
// The mask is R8_UNORM: 1 means interface (keep the original pixel), 0 means
// world (keep the neural output), and the values between are the feather band
// that MaskMath::CoverageAt produces. Blending rather than selecting is the
// whole point -- a hard boundary between a re-lit image and an untouched one is
// conspicuous, especially where it crosses a gradient.

Texture2D<float4>   g_original : register(t0);
Texture2D<float4>   g_neural   : register(t1);
Texture2D<float>    g_mask     : register(t2);
RWTexture2D<float4> g_dst      : register(u0);

cbuffer Dimensions : register(b0) {
  uint2 g_size;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
  if (id.x >= g_size.x || id.y >= g_size.y) return;

  const int3 at = int3(id.xy, 0);
  const float4 original = g_original.Load(at);
  const float4 neural = g_neural.Load(at);
  const float coverage = saturate(g_mask.Load(at));

  // lerp(neural, original, coverage): coverage 0 leaves the neural result
  // untouched, coverage 1 restores the original exactly. The exactness matters
  // -- the device test asserts a fully masked region is bit-identical to the
  // input, which only holds if coverage 1 contributes no neural component at
  // all.
  g_dst[id.xy] = lerp(neural, original, coverage);
}
