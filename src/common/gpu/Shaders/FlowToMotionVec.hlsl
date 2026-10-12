// Expands the NVOFA flow grid to full resolution, converts it into the
// motion-vector convention NGX expects, and grades how far each vector can be
// trusted.
//
// The sign here mirrors FlowToMotionPixels in MotionVectorMath.h exactly: NVOFA
// and NGX agree that a vector points at where the pixel was a frame ago, so the
// flow passes through unnegated unless g_sign asks for the diagnostic flip. If
// one changes, the other must change with it; the unit tests guard the C++ side
// and this shader is written to match it line for line.
//
// The distrust mask (1 = do not trust history here) exists because these
// vectors are estimated from two finished images, with no depth to tell the
// reprojection where something was uncovered. It marks three things:
//
//   off-screen   history would be read from outside the previous frame -- the
//                strip a rotating camera reveals at the screen edge;
//   photometric  the previous frame, fetched along the vector, does not look
//                like this pixel -- disocclusions, and vectors that are simply
//                wrong (spell effects, flames, transparency);
//   jump         neighbouring cells disagree sharply about motion -- the edge
//                of a character moving against the background, where one side
//                is being uncovered and a grid cell straddles both.
//
// It feeds DLSS as the bias-current-colour mask and the final blend as a
// fallback to the untouched frame, so a pixel with no usable history shows the
// present instead of a trail of the past.

Texture2D<int2>     g_flow     : register(t0);   // R16G16_SINT, S10.5 fixed point
Texture2D<float>    g_curLuma  : register(t1);   // R8_UNORM, this frame
Texture2D<float>    g_prevLuma : register(t2);   // R8_UNORM, the reference frame
RWTexture2D<float2> g_dst      : register(u0);   // RG16F, NDC
RWTexture2D<float>  g_distrust : register(u1);   // R8_UNORM, 1 = distrust
SamplerState        g_linear   : register(s0);

cbuffer Params : register(b0) {
  uint2 g_fullSize;
  uint2 g_gridSize;      // grid dimensions in cells
  uint  g_gridFactor;    // pixels per cell
  float g_sign;          // +1, or -1 for the diagnostic inversion
}

static const float kS10_5Scale = 32.0f;

// Luma differences below this are noise, compression and R8 quantisation.
static const float kPhotometricLow = 0.03f;
static const float kPhotometricHigh = 0.12f;
// Pixel disagreement between neighbouring cells that starts, and saturates, the
// motion-edge term. Parallax inside a rotating scene stays well under this.
static const float kJumpLow = 1.5f;
static const float kJumpHigh = 6.0f;

float2 CellMotionPixels(int2 cell) {
  cell = clamp(cell, int2(0, 0), int2(g_gridSize) - 1);
  return g_sign * float2(g_flow.Load(int3(cell, 0))) / kS10_5Scale;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
  if (id.x >= g_fullSize.x || id.y >= g_fullSize.y) return;

  const int2 cell = int2(min(id.xy / g_gridFactor, g_gridSize - 1));
  const float2 pixels = CellMotionPixels(cell);
  g_dst[id.xy] = pixels / float2(g_fullSize);

  // Where history for this pixel lives, in pixels and in UV.
  const float2 historyPos = float2(id.xy) + 0.5f + pixels;
  const float offscreen =
      (any(historyPos < 0.0f) || any(historyPos > float2(g_fullSize))) ? 1.0f : 0.0f;

  // Photometric: compare against the previous frame fetched along the vector.
  // The local contrast of the current frame is subtracted first, because a
  // sub-pixel error on a hard edge is a large luma difference and not a
  // disocclusion.
  const float current = g_curLuma.Load(int3(id.xy, 0));
  float lo = current;
  float hi = current;
  [unroll] for (int dy = -1; dy <= 1; ++dy) {
    [unroll] for (int dx = -1; dx <= 1; ++dx) {
      const int2 at = clamp(int2(id.xy) + int2(dx, dy), int2(0, 0),
                            int2(g_fullSize) - 1);
      const float v = g_curLuma.Load(int3(at, 0));
      lo = min(lo, v);
      hi = max(hi, v);
    }
  }
  const float previous =
      g_prevLuma.SampleLevel(g_linear, historyPos / float2(g_fullSize), 0.0f);
  const float error = max(0.0f, abs(current - previous) - 0.5f * (hi - lo));
  const float photometric = smoothstep(kPhotometricLow, kPhotometricHigh, error);

  // Motion edges: how sharply the neighbouring cells disagree with this one.
  float jump = 0.0f;
  jump = max(jump, length(CellMotionPixels(cell + int2(1, 0)) - pixels));
  jump = max(jump, length(CellMotionPixels(cell + int2(-1, 0)) - pixels));
  jump = max(jump, length(CellMotionPixels(cell + int2(0, 1)) - pixels));
  jump = max(jump, length(CellMotionPixels(cell + int2(0, -1)) - pixels));
  const float edge = smoothstep(kJumpLow, kJumpHigh, jump);

  g_distrust[id.xy] = max(offscreen, max(photometric, edge));
}
