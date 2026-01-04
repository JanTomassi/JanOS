#pragma once

#include <stdint.h>

void PIC_sendEOI(uint8_t irq);
void PIC_remap(int offset1, int offset2);
void pic_disable(void);
void pic_enable(void);
void pic_mask_irq(uint8_t irq);
void pic_unmask_irq(uint8_t irq);
