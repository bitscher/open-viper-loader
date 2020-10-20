#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/select.h>

enum operation {
	OP_UNSET,
	OP_READ,
	OP_WRITE,
	OP_COMPARE,
};

struct config {
	enum operation operation;
	bool safe_mode;
	uint16_t port;
	int serial; /* Serial device fd */
	fd_set serial_s;
	struct timeval timeout;
	char file_path[256];
	char serial_dev[256];
};

extern struct config g_cfg;

static inline bool use_serial()
{
	return g_cfg.serial != -1;
}

#define eprintf(...) fprintf(stderr, __VA_ARGS__)
