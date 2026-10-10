#pragma once

#include <cstdint>

namespace nzato::ndless {
class MonotonicClock {
public:
  void init();
  uint32_t now_ms();
  void shutdown();

private:
  volatile unsigned* value_{};
  volatile unsigned* control_{};
  volatile unsigned* source_{};
  uint32_t original_value_{};
  uint32_t original_control_{};
  uint32_t original_source_{};
  uint32_t last_value_{};
  uint32_t ticks_per_second_{};
  uint64_t elapsed_ticks_{};
  bool initialized_{};
};
} // namespace nzato::ndless
