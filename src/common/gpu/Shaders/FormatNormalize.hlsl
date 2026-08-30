// Converts the BGRA8_UNORM texture Windows Graphics Capture delivers into the
// explicitly typed RGBA16F the neural pass expects.
//
// No gamma conversion: DWM composites in sRGB-encoded 8-bit and the neural
// pass expects the same encoding, so this reorders channels and widens
// precision, nothing more.

Texture2D<float4>   g_src : register(t0);
RWTexture2D<float4> g_dst : register(u0);

cbuffer Dimensions : register(b0) {
  uint2 g_size;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
  if (id.x >= g_size.x || id.y >= g_size.y) return;
  // A BGRA8_UNORM SRV already presents channels as .rgba in shader order, so
  // no manual swizzle is needed. The test asserts this rather than assuming it.
  g_dst[id.xy] = g_src.Load(int3(id.xy, 0));
}
