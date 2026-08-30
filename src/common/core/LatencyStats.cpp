#include "core/LatencyStats.h"

#include <algorithm>
#include <cmath>

namespace sidecar {

void LatencyStats::Record(double ms) {
  samples_[next_] = ms;
  next_ = (next_ + 1) % kCapacity;
  if (count_ < kCapacity) ++count_;
}

void LatencyStats::RecordDrop() { ++dropped_; }

void LatencyStats::Reset() {
  next_ = 0;
  count_ = 0;
  dropped_ = 0;
}

double LatencyStats::Percentile(double fraction) const {
  if (count_ == 0) return 0.0;
  const size_t n = static_cast<size_t>(count_);
  std::array<double, kCapacity> sorted{};
  std::copy_n(samples_.begin(), n, sorted.begin());
  std::sort(sorted.begin(), sorted.begin() + n);
  // Nearest-rank: the smallest value at or above the requested fraction. Round
  // up rather than to nearest, so a tail percentile never under-reports.
  size_t rank = static_cast<size_t>(std::ceil(fraction * static_cast<double>(n)));
  if (rank == 0) rank = 1;
  if (rank > n) rank = n;
  return sorted[rank - 1];
}

double LatencyStats::P50() const { return Percentile(0.50); }
double LatencyStats::P99() const { return Percentile(0.99); }

}  // namespace sidecar
