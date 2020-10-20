#pragma once

#include <stdint.h>

int serial_init(void);

void serial_outb(uint8_t data);
uint8_t serial_inb(void);
int serial_read_byte_stream(uint8_t *bios_buffer, uint32_t max);
int serial_write_byte_stream(uint8_t *data, uint32_t data_sz);
