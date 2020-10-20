CC = gcc
CFLAGS = -O2 -Wall -Wextra -Wpedantic -Werror
DEPS = config.h arduino_serial.h
OBJ = viper_loader.o arduino_serial.o
TARGET = viper_loader

ARDUINO_FQBN = arduino:avr:nano:cpu=atmega328old
ARDUINO_IFACE = /dev/ttyUSB0
BAUD_RATE = 1000000

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS) -DBAUD_RATE=B${BAUD_RATE}

$(TARGET): $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: all clean arduino_compile arduino_upload

arduino_compile:
	arduino-cli compile --fqbn $(ARDUINO_FQBN) --warnings all --build-properties build.extra_flags="-O2 -DBAUD_RATE=${BAUD_RATE}" --verbose viper_arduino_bridge/

arduino_upload: arduino_compile
	arduino-cli upload --fqbn $(ARDUINO_FQBN) --port $(ARDUINO_IFACE) --verbose viper_arduino_bridge/

all: $(TARGET)

clean:
	rm -f *.o $(TARGET)
	rm -fr viper_arduino_bridge/build/
