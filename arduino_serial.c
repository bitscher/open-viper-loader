#include "config.h"

#include <termio.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#ifndef BAUD_RATE
#define BAUD_RATE B1000000
#endif

/*
 * Since only the 6 least significant bits are used by outb we can use the
 * most 2 significant to command the Arduino:
 *    - 0 : outb
 *    - 1 : inb
 *    - 2 : read n bytes
 *    - 3 : write n bytes
 *
 * See viper_arduino_bridge.ino for more details
 */

static int serial_wait_data(const struct timeval *timeout, bool silent_timeout)
{
	struct timeval to = (timeout) ? *timeout : g_cfg.timeout;
	int r;

	r = select(g_cfg.serial + 1, &g_cfg.serial_s, NULL, NULL, &to);
	if (r == -1) {
		perror("Select error");
		return -1;
	} else if (r == 0) {
		if (!silent_timeout)
			eprintf("\nArduino timed out\n");
		return -2;
	}
	return 0;
}

static int serial_try_init(bool first_run)
{
	struct termios tty;
	unsigned char ping = 0x40; /* inb */
	int r = 0;

	g_cfg.serial = open(g_cfg.serial_dev, O_RDWR);
	if (g_cfg.serial == -1) {
		perror("Failed to open serial device, make sure to give your "
		       "user access to the device or run as root\n");
		return 1;
	}
	if (tcgetattr(g_cfg.serial, &tty) == -1) {
		perror("Failed to get TTY attributes, wrong device?");
		return 1;
	}

	if (cfsetspeed(&tty, BAUD_RATE) == -1) {
		perror("Failed to set baud rate\n");
		close(g_cfg.serial);
		return 1;
	}

	tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR |
			 ICRNL | IXON | IXOFF);
	tty.c_oflag &= ~OPOST;
	tty.c_cflag &= ~(CSIZE | PARENB | HUPCL);
	tty.c_cflag |= CS8;
	tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	tty.c_cc[VMIN] = 0;
	tty.c_cc[VTIME] = 0;

	if (tcsetattr (g_cfg.serial, TCSANOW, &tty) != 0) {
		perror("Error configuring serial interface\n");
		close(g_cfg.serial);
		return 1;
	}

	FD_ZERO(&g_cfg.serial_s);
	FD_SET(g_cfg.serial, &g_cfg.serial_s);

	write(g_cfg.serial, &ping, 1);
	r = serial_wait_data(NULL, first_run);
	if (r != 0)
		return -r;
	tcflush(g_cfg.serial, TCIOFLUSH);

	printf("Ready\n");
	return 0;
}

int serial_init(void)
{
	int r;

	printf("Initializing serial interface %s... ", g_cfg.serial_dev);
	fflush(stdout);
	r = serial_try_init(true);
	if (r == 0 || r == 1)
		return r;
	if (r == 2) {
		usleep(1000000);
		return serial_try_init(false);
	}
	return 1;
}

void serial_outb(uint8_t data)
{
	unsigned char cmd = data & 0x3f;

	if (write(g_cfg.serial, &cmd, 1) <= 0) {
		perror("Serial write failure");
	}
}

uint8_t serial_inb(void)
{
	uint8_t data = 0xff;
	uint8_t cmd = 0x40;

	if (write(g_cfg.serial, &cmd, 1) <= 0) {
		perror("Serial write failure");
		return 1;
	}

	if (serial_wait_data(NULL, false))
		return 1;
	if (read(g_cfg.serial, &data, 1) != 1) {
		eprintf("Serial read failure %u\n", __LINE__);
		return 1;
	}
	return data;
}

uint8_t serial_read_byte_stream(uint8_t *bios_buffer, uint32_t max)
{
	uint8_t read_cmd[3];

	read_cmd[0] = 0x80 | max >> 16;
	read_cmd[1] = (max >> 8) & 0xff;
	read_cmd[2] = max & 0xff;
	if (write(g_cfg.serial, &read_cmd, 3) <= 0) {
		perror("Serial write failure");
		return 1;
	}

	for (uint32_t i = 0; i < max; ) {
		if (serial_wait_data(NULL, false))
			return 1;

		i += read(g_cfg.serial, &bios_buffer[i], max - i);
		printf("\rReceived %06u/%06u bytes", i, max);
	}
	return 0;
}

int serial_write_byte_stream(uint8_t *data, uint32_t data_sz)
{
	uint8_t write_cmd[3];
	uint8_t ack = 0x69;
	int r;
	struct timeval timeout = {
		.tv_sec = 5,
	};

	write_cmd[0] = 0xc0 | data_sz >> 16;
	write_cmd[1] = (data_sz >> 8) & 0xff;
	write_cmd[2] = data_sz & 0xff;

	if (write(g_cfg.serial, &write_cmd, 3) <= 0) {
		perror("Serial write failure");
		return 1;
	}

	for (uint32_t i = 0; i < data_sz; i += 60) {
		uint32_t left = data_sz - i;

		uint32_t write_sz = left < 60 ? left : 60;
		if (write(g_cfg.serial, &data[i], write_sz) <= 0) {
			perror("Serial write failure");
			return 1;
		}
		/* Add padding */
		if (left < 60) {
			uint8_t padding[59] = {0};

			write(g_cfg.serial, padding, 60 - left);
		}

		if (serial_wait_data(&timeout, false))
			return 1;
		r = read(g_cfg.serial, &ack, 1);
		if (r != 1 || ack != 60) {
			eprintf("Serial read failure r=%d %u %02x\n", r,
				__LINE__, ack);
			return 1;
		}
		printf("\rWritten %06u/%06u bytes", i + write_sz, data_sz);
	}

	printf("\n");
	return 0;
}
