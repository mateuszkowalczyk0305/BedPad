#include "input_math.h"

int8_t input_axis_delta(int raw, int center, int dead_zone, int max_step,
                        bool invert) {
  int displacement = raw - center;
  int magnitude = displacement < 0 ? -displacement : displacement;
  if (magnitude <= dead_zone || max_step <= 0) {
    return 0;
  }
  int range = (displacement < 0 ? center : 4095 - center) - dead_zone;
  if (range <= 0) {
    return 0;
  }
  if (max_step > 127) {
    max_step = 127;
  }
  int step = 1 + (magnitude - dead_zone - 1) * (max_step - 1) / range;
  if (magnitude - dead_zone >= range || step > max_step) {
    step = max_step;
  }
  if (displacement < 0) {
    step = -step;
  }
  return (int8_t)(invert ? -step : step);
}
