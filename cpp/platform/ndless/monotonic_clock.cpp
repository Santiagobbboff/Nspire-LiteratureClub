#include "monotonic_clock.hpp"

namespace nzato::ndless {
void MonotonicClock::init() {
  shutdown();
  value_ = reinterpret_cast<volatile unsigned*>(0x900C0004U);
  control_ = reinterpret_cast<volatile unsigned*>(0x900C0008U);
  source_ = reinterpret_cast<volatile unsigned*>(0x900C0080U);
  ticks_per_second_ = 32768;
  original_value_ = *value_;
  original_control_ = *control_;
  original_source_ = *source_;
  *control_ = 0;
  *source_ = 0x0AU;
  *control_ = 0x82U;
  last_value_ = *value_;
  elapsed_ticks_ = 0;
  initialized_ = true;
}

uint32_t MonotonicClock::now_ms() {
  if (!initialized_)
    return 0;
  const uint32_t current = *value_;
  elapsed_ticks_ += static_cast<uint32_t>(last_value_ - current);
  last_value_ = current;
  return static_cast<uint32_t>((elapsed_ticks_ * 1000ULL) / ticks_per_second_);
}

void MonotonicClock::shutdown() {
  if (!initialized_)
    return;
  *control_ = 0;
  *source_ = original_source_;
  *value_ = original_value_;
  *control_ = original_control_;
  value_ = control_ = source_ = nullptr;
  initialized_ = false;
}
} // namespace nzato::ndless
