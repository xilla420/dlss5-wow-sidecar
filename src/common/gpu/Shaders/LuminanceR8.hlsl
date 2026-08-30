// Reduces the captured BGRA8 frame to single-channel luminance.
//
// NVOFA's D3D12 path takes GRAYSCALE8 input, so the colour frame cannot be fed
// to it directly. Rec.601 luma is what the SDK's own samples use, and optical
// flow only cares about structure, not colorimetry.

Texture2D<float4>  g_src : register(t0);
RWTexture2D<float> g_dst : register(u0);

cbuffer Dimensions : register(b0) {
  uint2 g_size;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
  if (id.x >= g_size.x || id.y >= g_size.y) return;
  const float3 rgb = g_src.Load(int3(id.xy, 0)).rgb;
  g_dst[id.xy] = dot(rgb, float3(0.299f, 0.587f, 0.114f));
}
