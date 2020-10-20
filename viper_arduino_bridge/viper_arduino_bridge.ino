/*
 * Pin wiring:
 *  +----------------+--------------+
 *  |  DB-25 (Name)  | Arduino Nano |
 *  +----------------+--------------|
 *  |   2  (D0)      |  Digital D2  |
 *  |   3  (D1)      |  Digital D3  |
 *  |   4  (D2)      |  Digital D4  |
 *  |   5  (D3)      |  Digital D5  |
 *  |   6  (D4)      |  Digital D6  |
 *  |   7  (D5)      |  Digital D7  |
 *  |  13  (SELECT)  |  Digital D8  |
 *  |  15  (ERROR)   |  Digital D9  |
 *  |  25  (GND)     |  GND         |
 *  +----------------+--------------+
 *
 * Commands are read on the serial port:
 *   - 0b00nnnnnn: Output nnnnnn on data pins
 *   - 0b01xxxxxx: Read pin 13 and 15 and return a byte formatted like the
 *                 status register of a parallel port.
 *   - 0b80xxxxxn 0x12 0x34: Read 0xn1234 from to the chip starting from
 *                 address 0, data is sent back byte by byte on the serial port
 *   - 0bC0xxxxxn 0xAB 0xCD: Write 0xnABCD bytes to the chip starting from
 *                 address 0, data is received in chunks on the serial port
 *
 * Refer to arduino_serial.c for more information on the client-side
 */

static const uint8_t PIN_SEL = 8;	/* D8 */
static const uint8_t PIN_ERR = 9;	/* D9 */

#ifndef BAUD_RATE
#define BAUD_RATE 1000000
#endif

void setup()
{
	DDRD = DDRD | B11111100;
	pinMode(PIN_ERR, INPUT_PULLUP);
	pinMode(PIN_SEL, INPUT_PULLUP);

	Serial.setTimeout(2000);
	Serial.begin(BAUD_RATE);
}

static inline unsigned char serial_read_one_byte()
{
	int b;

	do {
		b = Serial.read();
	} while (b == -1);
	return (unsigned char) b;
}

static void outb(uint8_t data)
{
	PORTD = (((uint8_t) data) << 2) | (PORTD & 0x3);
}

static void inb()
{
	uint8_t r = 0;

	if (digitalRead(PIN_SEL) == HIGH)
		r |= 0x10; /* 0001 0000 = PIN 13 */
	if (digitalRead(PIN_ERR) == HIGH)
		r |= 0x08; /* 0000 1000 = PIN 15 */

	Serial.write(r);
}

static int safe_mode_check(bool check_high)
{
	static const uint8_t MAX_TRIES = 4;

	/* Chip ACKs by setting pin 15 to high */
	for (uint8_t tries = 0; tries < MAX_TRIES; ++tries) {
		if ((check_high && digitalRead(PIN_ERR) != LOW)
		    || (!check_high && digitalRead(PIN_ERR) != HIGH))
			return 0;
		delay(1); /* 1ms */
	}
	return 1;
}

/* Writes 5 bits (a pentad) encoded on 6 wires and check for errors */
static int outp(uint8_t data)
{
	uint8_t formatted_data = data & 0xf;

	if (data & 0x10)
		formatted_data = formatted_data | 0x20;

	outb(formatted_data);
	if (safe_mode_check(true))
		return 1;
	outb(formatted_data | 0x10);
	if (safe_mode_check(false))
		return 1;
	return 0;
}

static uint32_t read_size(uint8_t first)
{
	uint8_t size[2] = {0};

	Serial.readBytes(size, 2);
	first &= 0x3f;
	return (uint32_t) first << 16 | (uint32_t) size[0] << 8 | size[1];
}

static int read_byte_stream(uint8_t first)
{
	uint8_t data = 0;
	const uint8_t CMD_READ = 0x0d;
	uint32_t total = read_size(first);

	for (uint32_t b = 0; b < total; ++b) {
		if (outp(CMD_READ))
			return 1;
		for (uint8_t i = 0; i < 8; ++i) {
			data = data >> 1;
			if (digitalRead(PIN_SEL) == HIGH)
				data |= 0x80;
			/* Acknowledge we've read bit number i */
			if (outp(i))
				return 1;
		}
		Serial.write(data);
	}
	return 0;
}

static int write_byte(uint32_t address, uint8_t data)
{
	const uint8_t CMD_WRITE_BYTE = 0x05;

	/*
	 * Flash default value is 0xff, skip bytes that already have the correct
	 * value and save some time
	 */
	if (data == 0xff)
		return 0;

	if (outp(CMD_WRITE_BYTE))
		return 1;
	/*  First 3 most significant bits of data + 2 MSBs of address */
	if (outp(((data >> 3) & 0x1c) | address >> 15))
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

static int write_byte_stream(uint8_t first)
{
	uint8_t data_buffer[60];
	uint32_t total = read_size(first);

	for (uint32_t i = 0; i < total; ++i)
	{
		if (i % 60 == 0) {
			/* Refill work buffer from serial buffer */
			uint8_t r = Serial.readBytes(data_buffer, 60);

			Serial.write(r); /* Notify client to send more */
			if (r < 60)
				return 1;
		}
		if (write_byte(i, data_buffer[i % 60]))
			return 1;
	}
	return 0;
}

void loop()
{
	uint8_t d = serial_read_one_byte();

	switch(d & 0xC0) {
	case 0x00: /* out/write operation */
		outb(d);
		break;
	case 0x40: /* in/read operation */
		inb();
		break;
	case 0x80: /* Accelerated function to write a stream of bytes */
		if (read_byte_stream(d))
			delay(10000); /* Sleep 10s to timeout the PC */
		outp(0x0);
		break;
	case 0xC0: /* Accelerated function to write a stream of byte */
		if (write_byte_stream(d))
			delay(10000); /* Sleep 10s to timeout the PC */
		outp(0x0);
		break;
	default:
		break;
	}

}
