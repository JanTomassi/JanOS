#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef void (*irq_handler_t)(uint8_t irq_line, void *context);

bool irq_register_handler(uint8_t irq_line, irq_handler_t handler, void *context);
bool irq_unregister_handler(uint8_t irq_line, irq_handler_t handler, void *context);
