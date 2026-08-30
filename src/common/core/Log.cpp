#include "core/Log.h"

namespace sidecar {
namespace {

const char* LevelName(LogLevel level) {
  switch (level) {
    case LogLevel::Warn:  return "WARN";
    case LogLevel::Error: return "ERROR";
    default:              return "INFO";
  }
}

}  // namespace

bool Log::OpenFile(const std::filesystem::path& path) {
  std::lock_guard<std::mutex> lock(mutex_);
  file_.open(path, std::ios::out | std::ios::trunc);
  return file_.is_open();
}

void Log::SetMinimumLevel(LogLevel level) {
  std::lock_guard<std::mutex> lock(mutex_);
  minimum_ = level;
}

void Log::Write(LogLevel level, std::string_view message) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (level < minimum_) return;

  std::string line;
  line.reserve(message.size() + 8);
  line += LevelName(level);
  line += ": ";
  line.append(message);

  // Overwriting a line the operator has not read yet is a loss worth
  // reporting, so it is counted rather than silently discarded.
  if (count_ == kCapacity) ++dropped_;
  lines_[next_] = line;
  next_ = (next_ + 1) % kCapacity;
  if (count_ < kCapacity) ++count_;

  if (level == LogLevel::Error) lastError_ = line;

  if (file_.is_open()) {
    file_ << line << '\n';
    file_.flush();   // a crash must not take the reason for it with it
  }
}

std::vector<std::string> Log::Recent() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> out;
  out.reserve(count_);
  // Walk from the oldest surviving entry so callers get chronological order.
  const size_t first = (count_ == kCapacity) ? next_ : 0;
  for (size_t i = 0; i < count_; ++i) {
    out.push_back(lines_[(first + i) % kCapacity]);
  }
  return out;
}

std::string Log::LastError() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lastError_;
}

uint64_t Log::Dropped() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return dropped_;
}

void Log::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& line : lines_) line.clear();
  next_ = 0;
  count_ = 0;
  dropped_ = 0;
  lastError_.clear();
}

Log& GlobalLog() {
  static Log instance;
  return instance;
}

}  // namespace sidecar
