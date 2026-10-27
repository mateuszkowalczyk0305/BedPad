#pragma once
#include <stdbool.h>
#include <stdint.h>

int8_t input_axis_delta(int raw, int center, int dead_zone, int max_step,
                        bool invert);
