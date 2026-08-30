#pragma once
#include <array>
#include <cstdint>

namespace sidecar {

// Fixed-capacity so Record() never allocates on the render thread.
class LatencyStats {
 public:
  static constexpr size_t kCapacity = 1024;

  void Record(double ms);
  void RecordDrop();
  void Reset();

  double P50() const;
  double P99() const;
  uint64_t Dropped() const { return dropped_; }
  uint64_t Count() const { return count_; }

 private:
  double Percentile(double fraction) const;

  std::array<double, kCapacity> samples_{};
  size_t next_ = 0;
  uint64_t count_ = 0;     // number of live samples, capped at kCapacity
  uint64_t dropped_ = 0;
};

}  // namespace sidecar
