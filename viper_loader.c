#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/io.h>
#include <string.h>

#include "config.h"
#include "arduino_serial.h"

/*
 * Technical overview:
 *
 * The only DB-25 pins required for a succesful communication are 2 to 7, 13,
 * 15, 25 (or any other ground pin 18-25)
 *
 * The Viper GC does not use bidirectional data lines, instead it always
 * receives data on pins D0-D5 and sends data on pin 13 (Select), error control
 * happens on pin 15 (Error).
 * As a result we have a 6 bits wide interface from the computer to the Viper GC
 * (of which only 5 carry actual data, bit 5 is used for signaling) and a 2 bit
 * interface from the Viper GC to the computer (of which only 1 carries data).
 *
 * Most Computers don't come with a parallel port nowadays so this program can
 * work by using an Arduino as a bridge to the parallel module of the Viper GC.
 * It is slightly slower than a full fledged parallel port but mostly works.
 * See in folder arduino_bridge/ for more details.
 */

static void usage_exit(const char *p, int exit_code)
{
	printf("Usage: %s [-h] [-u] [-p port] [-s dev] (-r out_file | -w in_file | -c in_file)\n", p);
	printf("\t-r out_file: Dump the content of the modchip into out_file\n");
	printf("\t-w in_file: Write the content of in_file into the modchip\n");
	printf("\t-c in_file: Compare the content of in_file with the content of the modchip\n");
	printf("Options:\n");
	printf("\t-u: Disable safe mode\n");
	printf("\t-p: Use specified IO port address in hexadecimal (default is 0x378)\n");
	printf("\t-s: Use Arduino serial bridge connected to dev (example /dev/ttyUSB0)\n");
	printf("\t-h: Displays this usage message\n");
	exit(exit_code);
}

static const uint8_t CMD_RESET		= 0x00;
static const uint8_t CMD_ERASE		= 0x03;
static const uint8_t CMD_WRITE_BYTE	= 0x05;
static const uint8_t CMD_READ_INIT[]	= {0x11, 0x00, 0x00, 0x00, 0x00};
static const uint8_t CMD_READ		= 0x0d;
static const uint8_t CMD_CHIP_INIT[]    = {0xff, 0x0c, 0x12};

static const uint8_t MASK_CHIP_DATA	= 0x10;	/* PIN 13 in status register */
static const uint8_t MASK_CHIP_ERR	= 0x08;	/* PIN 15 in status register */

struct config g_cfg = {
	.operation = OP_UNSET,
	.port = 0x378,
	.serial = -1,
	.safe_mode = true,
	.timeout = {
		.tv_sec = 1,
	},
};
static const uint32_t BIOS_SIZE = (uint32_t) 0x20000;

static void _outb(uint8_t data)
{
	if (use_serial())
		serial_outb(data);
	else
		outb(data, g_cfg.port);
}

static uint8_t _inb()
{
	if (use_serial())
		return serial_inb();
	else
		return inb(g_cfg.port + 1);
}

static int safe_mode_check(bool high)
{
	static const uint8_t MAX_TRIES = 4;

	/* Chip ACKs by setting pin 15 to high */
	for (uint8_t tries = 0; tries < MAX_TRIES; ++tries) {
		uint8_t r = _inb() & MASK_CHIP_ERR;
		if ((high && r != 0) || (!high && r == 0))
			return 0;
		usleep((1 << tries) * 125); /* 125us, 250us, 500us */
	}
	return 1;
}

/* Writes 5 bits (a pentad) encoded on 6 wires and check for errors */
static int outp(uint8_t data)
{
	uint8_t formatted_data = data & 0xf;

	if (data & 0x10)
		formatted_data = formatted_data | 0x20;

	_outb(formatted_data);
	if (g_cfg.safe_mode && safe_mode_check(true))
		return 1;
	_outb(formatted_data | 0x10);
	if (g_cfg.safe_mode && safe_mode_check(false))
		return 1;
	return 0;
}

/* Reads next byte in order from the chip. */
static int read_byte(uint8_t *out)
{
	uint8_t val, data = 0;

	if (outp(CMD_READ)) {
		eprintf("CMD_READ failed in read_byte\n");
		return 1;
	}
	for (uint8_t i = 0; i < 8; ++i) {
		val = _inb();
		/* Only keep bit 4 (Pin 13) and append it to data, 8 times to
		   rebuild a full byte. Least significant is read first. */
		data = ((val & MASK_CHIP_DATA) << 3) | data >> 1;
		/* Acknowledge we've read bit number i */
		if (outp(i)) {
			eprintf("Error while ack bit %u\n", i);
			return 1;
		}
	}
	*out = data;
	return 0;
}

/*
 * Sets the chip in read mode and ready to receive read commands
 * From there on every time CMD_READ is received by the chip it will output
 * the next byte incrementally from address 0x0 to address 0x1ffff
 */
static int init_read_mode()
{
	return outp(CMD_READ_INIT[0]) || outp(CMD_READ_INIT[1])
	    || outp(CMD_READ_INIT[2]) || outp(CMD_READ_INIT[3])
	    || outp(CMD_READ_INIT[4]);
}

static int read_bios()
{
	FILE *file;
	uint8_t bios_buffer[BIOS_SIZE];
	const uint32_t one_percent = BIOS_SIZE / 100;
	uint32_t i = 0;

	printf("Reading bios to file %s\n", g_cfg.file_path);
	if (init_read_mode()) {
		eprintf("Error while initializing the chip for reading\n");
		outp(CMD_RESET);
		return 1;
	}
	printf("Reading...\n");

	/*
	* If using the Arduino bridge, use the Arduino-accelerated
	* version to reduce serial communication bottlenecks.
	*/
	if (use_serial() && serial_read_byte_stream(bios_buffer, BIOS_SIZE)) {
		fflush(stdout);
		eprintf("\nError while reading from the chip.\n");
	}
	for (i = 0; !use_serial() && i < BIOS_SIZE; i++) {
		if (read_byte(&bios_buffer[i])) {
			eprintf("Error while reading at address 0x%05x\n", i);
			break;
		}

		if (i % one_percent == 0) {
			printf("\r%02u%% done", i / one_percent);
			fflush(stdout);
		}
	}

	outp(CMD_RESET);
	printf("\nRead complete\n");
	file = fopen(g_cfg.file_path, "wb+");
	if (!file) {
		eprintf("Couldn't create file '%s', check access rights\n",
			g_cfg.file_path);
		return 1;
	}
	fwrite(bios_buffer, 1, BIOS_SIZE, file);
	fclose(file);
	return 0;
}

/* Writes a byte of data at a given address */
static int write_byte(uint8_t data, uint32_t address)
{
	/*
	 * Flash default value is 0xff, skip bytes that already have the correct
	 * value and save some time
	 */
	if (data == 0xff)
		return 0;

	address = address & 0x1ffff;

	if (outp(CMD_WRITE_BYTE))
		return 1;
	/*  First 3 most significant bits of data + 2 MSBs of address */
	if (outp(((data >> 3) & 0x1c) | (address >> 15)))
		return 1;
	/* Then 3x5 remaining bits of address for a total of 17 address bits */
	if (outp(address >> 10))
		return 1;
	if (outp(address >> 5))
		return 1;
	if (outp(address))
		return 1;
	/* Then the rest of the data (5 bits) */
	for (uint8_t i = 0; i < 4; ++i)
		outp(data);
	return 0;
}

static void erase_chip()
{
	uint8_t c1, c2;

	printf("Erasing memory... ");

	for (uint8_t i = 0; i < 13; ++i)
		outp(CMD_ERASE);

	init_read_mode();
	read_byte(&c2);
	do {
		c1 = c2;
		init_read_mode();
		read_byte(&c2);
	} while (c1 != c2);
	printf("Done\n");
}

static ssize_t load_bios_file(void *buffer)
{
	size_t size, read;
	FILE *f = fopen(g_cfg.file_path, "rb");

	if (!f) {
		eprintf("Could not open file '%s'\n", g_cfg.file_path);
		return -1;
	}
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	if (size > BIOS_SIZE) {
		eprintf("File '%s' of size %zu won't fit on the chip\n",
			g_cfg.file_path, size);
		return -1;
	}
	fseek(f, 0, SEEK_SET);
	read = fread(buffer, 1, size, f);
	fclose(f);
	if (read != size) {
		eprintf("Failed to read all bytes from '%s'\n",
			g_cfg.file_path);
		return -1;
	}
	return size;
}

static int write_bios()
{
	uint8_t buffer[BIOS_SIZE];
	ssize_t size = load_bios_file(buffer);
	const uint32_t one_percent = size / 100;

	if (size <= 0)
		return 1;

	erase_chip();
	usleep(1000000); // 1 second

	printf("Flashing memory...\n");

	/*
	 * If using the Arduino bridge, use the Arduino-accelerated version to
	 * avoid serial communication bottlenecks.
	 */
	if (use_serial() && serial_write_byte_stream(buffer, size)) {
		fflush(stdout);
		eprintf("\nError while writing to the chip.\n");
		return 1;
	}

	/* Main writing loop */
	for (uint32_t i = 0; !use_serial() && i < (uint32_t) size; ++i) {
		if (write_byte(buffer[i], i)) {
			eprintf("Error while writing to the chip. "
				"@0x%05x <- 0x%02x\n", i, buffer[i]);
			/* Give it a second chance */
			if (write_byte(buffer[i], i))
				return 1;
		}

		if (i % one_percent == 0) {
			printf("\r%02u%% done", i / one_percent);
			fflush(stdout);
		}
	}
	outp(CMD_RESET);
	printf("\nFlash complete.\n");
	return 0;
}

static int compare_bios()
{
	uint8_t expect[BIOS_SIZE], actual[BIOS_SIZE];
	ssize_t file_size = load_bios_file(expect);
	const uint32_t one_percent = file_size / 100;

	if (file_size <= 0)
		return 1;

	printf("Comparing memory and file '%s'\n", g_cfg.file_path);

	if (init_read_mode()) {
		eprintf("Error while initializing the chip for reading\n");
		return 1;
	}

	if (use_serial()) {
		if (serial_read_byte_stream(actual, file_size))
			return 1;
		if (memcmp(expect, actual, file_size)) {
			size_t i = 0;

			while (expect[i] == actual[i] && i < (size_t) file_size)
				++i;
			if (i < (size_t) file_size) {
				fflush(stdout);
				eprintf("\nFirst difference found at address "
					"0x%05zx\n", i);
				return 1;
			}
		}
	}

	for (uint32_t i = 0; !use_serial() && i < (uint32_t) file_size; ++i) {
		uint8_t data = 0;

		if (read_byte(&data)) {
			eprintf("Error while reading from the chip.\n");
			return 1;
		}
		if (data != expect[i]) {
			eprintf("First difference found at address 0x%x\n", i);
			return 1;
		}
		if (i % one_percent == 0) {
			printf("\r%02u%% done", i / one_percent);
			fflush(stdout);
		}
	}
	printf("\nFile and memory are identical.\n");
	return 0;
}

static void set_operation(enum operation op, const char *file, const char *prog)
{
	if (g_cfg.operation != OP_UNSET)
		usage_exit(prog, EXIT_FAILURE);
	g_cfg.operation = op;
	if (strlen(file) >= sizeof(g_cfg.file_path)) {
		eprintf("File path is too long");
		exit(EXIT_FAILURE);
	}
	strncpy(g_cfg.file_path, file, sizeof(g_cfg.file_path));
}

static void process_config(int argc, char **argv)
{
	int opt;

	while ((opt = getopt(argc, argv, "up:s:r:w:c:h")) != -1) {
		switch (opt) {
		case 'u':
			g_cfg.safe_mode = false;
			break;
		case 'p': {
			char *endptr;
			unsigned long int val;

			val = strtoul(optarg, &endptr, 16);
			if (*endptr != '\0' || val == 0 || val > 0xffff)
				usage_exit(argv[0], EXIT_FAILURE);
			g_cfg.port = (uint16_t) val;
			break;
		}
		case 's':
			strncpy(g_cfg.serial_dev, optarg,
				sizeof(g_cfg.serial_dev) - 1);
			break;
		case 'r':
			set_operation(OP_READ, optarg, argv[0]);
			break;
		case 'w':
			set_operation(OP_WRITE, optarg, argv[0]);
			break;
		case 'c':
			set_operation(OP_COMPARE, optarg, argv[0]);
			break;
		case 'h':
			usage_exit(argv[0], EXIT_SUCCESS);
			break;
		default:
			usage_exit(argv[0], EXIT_FAILURE);
		}
	}
	if (g_cfg.operation == OP_UNSET)
		usage_exit(argv[0], EXIT_FAILURE);
}

int main(int argc, char **argv)
{
	process_config(argc, argv);

	/* Use parallel port if Serial hasn't been initialized */
	if (!g_cfg.serial_dev[0] && ioperm(g_cfg.port, 2, 1)) {
		eprintf("Unable to acquire permissions for port 0x%x, give "
			"yourself permission or try running as root.\n",
			g_cfg.port);
		return EXIT_FAILURE;
	} else if (g_cfg.serial_dev[0] && serial_init()) {
		return EXIT_FAILURE;
	}

	if (use_serial() && !g_cfg.safe_mode) {
		printf("WARNING: The Arduino program enforces safe mode. "
		       "To disable safe mode, early return 0 in function "
		       "safe_mode_check() in viper_arduino_bridge.ino\n");
	}

	/* Init Viper GC chip */
	outp(CMD_RESET);
	if (outp(CMD_CHIP_INIT[0]) || outp(CMD_CHIP_INIT[1])
	    || outp(CMD_CHIP_INIT[2])) {
		eprintf("Viper GC not found.\n");
		return EXIT_FAILURE;
	}

	switch (g_cfg.operation) {
		case OP_READ:
			return read_bios();
		case OP_WRITE:
			return write_bios();
		case OP_COMPARE:
			return compare_bios();
		default:
			usage_exit(argv[0], EXIT_FAILURE);
	}
}
