#pragma once

#include <stdint.h>
#include "driver/rmt_rx.h"

#define EXAMPLE_IR_RESOLUTION_HZ     1000000 // 1MHz resolution, 1 tick = 1us
#define EXAMPLE_IR_RX_GPIO_NUM       19

int example_parse_nec_frame(rmt_symbol_word_t *rmt_nec_symbols, size_t symbol_num);
