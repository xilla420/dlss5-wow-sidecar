// Expands the NVOFA flow grid to full resolution and converts it into the
// motion-vector convention NGX expects.
//
// The negation here mirrors FlowToMotionPixels in MotionVectorMath.h exactly.
// If one changes, the other must change with it; the unit tests guard the C++
// side and this shader is written to match it line for line.

ByteAddressBuffer   g_flow : register(t0);   // int16 pairs, S10.5
RWTexture2D<float2> g_dst  : register(u0);   // RG16F, NDC

cbuffer Params : register(b0) {
  uint2 g_fullSize;
  uint2 g_gridSize;      // grid dimensions in cells
  uint  g_gridFactor;    // pixels per cell
  uint  g_pad;
}

static const float kS10_5Scale = 32.0f;

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
  if (id.x >= g_fullSize.x || id.y >= g_fullSize.y) return;

  const uint2 cell = min(id.xy / g_gridFactor, g_gridSize - 1);
  const uint index = cell.y * g_gridSize.x + cell.x;

  // Two int16 per vector, so four bytes per cell.
  const uint packed = g_flow.Load(index * 4);
  const int rawX = (int)(packed << 16) >> 16;   // sign-extend the low half
  const int rawY = (int)packed >> 16;           // sign-extend the high half

  const float2 pixels = -float2(rawX, rawY) / kS10_5Scale;
  g_dst[id.xy] = pixels / float2(g_fullSize);
}
