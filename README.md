# Open Viper Loader

This project is meant to be an open source replacement of the
[Viper Loader](https://github.com/JakobAir/ViperGC/tree/master/Viper_Loader)
program that was distributed with the first generation of
[Viper GC modchip](https://www.gc-forever.com/wiki/index.php?title=Viper_GC)
for the Nintendo GameCube.

The first generation of Viper GC chips was programmed through a parallel port
interface but was quickly replaced by a USB interface.
The original Viper Loader is a Windows-only program that requires a parallel
port to be mapped on the I/O address 0x278, 0x378 or 0x3BC, it also shipped with
a version of inpout32.dll that does not work on Windows x86-64 (Note that
[Inpout32 v1.5.0.1](http://www.highrez.co.uk/downloads/inpout32/) works as a replacement).

This project contains two parts:
  - A replacement program for flashing the chip through a good old parallel port
    (Untested on an actual parallel port but I'm fairly confident it works, let
    me know if you happen to test it).
  - An Arduino sketch that acts as a serial bridge between the loader and the
    parallel port module of the Viper GC if you don't have a parallel port.

## About the Loader:
It's a reverse engineer of the original loader made possible thanks to
[Ghidra](https://github.com/NationalSecurityAgency/ghidra).
In its current state the project will only compile on Linux because it makes
calls to `ioperm` and `inb`/`outb` to access the parallel port (this can probably be
easily  changed since some of these function are already wrapped to allow the
use of a serial link to the Arduino).
The serial link is handled by `termios` and should build on other POSIX systems.

### Build
Simply run `make`

### Run
```bash
Usage: ./viper_loader [-h] [-u] [-p port] [-s dev] (-r out_file | -w in_file | -c in_file)
	-r out_file: Dump the content of the modchip into out_file
	-w in_file: Write the content of in_file into the modchip
	-c in_file: Compare the content of in_file with the content of the modchip
Options:
	-u: Disable safe mode
	-p: Use specified IO port address in hexadecimal (default is 0x378)
	-s: Use Arduino serial bridge connected to dev (example /dev/ttyUSB0)
	-h: Displays this usage message
```
#### Examples with parallel port
Write a file with parallel port on I/O address `0x278`:
```bash
./viper_loader -p 0x278 -w ~/banana.vgc
```
After writing, compare the original file with the content of the chip:
```bash
./viper_loader -p 0x278 -c ~/banana.vgc
```

#### Examples with serial connection to Arduino
Write a file with the Arduino interface connected to `/dev/ttyUSB0`
```bash
./viper_loader -s /dev/ttyUSB0 -w ~/apple.vgc
```

After writing, compare the original file with the content of the chip:
```bash
./viper_loader -s /dev/ttyUSB0 -c ~/apple.vgc
```

## About the Arduino interface:
It started as a simple replacement for `inb` and `outb` but the performance was
dreadful (~2 hours to write/read 128 KB) because too much useless blocking IO
was being done between the computer and the Arduino. To remediate that, two
additional functions have been embedded on the microcontroller in order to keep
communication to a minimum, resulting in write/read times of about 18 seconds
for 128 KB.

### Build and upload
The Makefile relies on [arduino-cli](https://github.com/arduino/arduino-cli) and
so should you. It is a lot more convenient that the regular Arduino GUI if you
know your way across the Linux command line. That being said you can load
`viper_arduino_bridge.ino` in the Arduino GUI to compile and upload it.

```bash
make arduino_upload
```
This will build the sketch for an `Arduino Nano` with the _old bootloader_ and
upload it on default serial port `/dev/ttyUSB0` and uses a `Baud Rate of
1000000` to communicate.
Adapt these values to your environement by editing the following variables in
the `Makefile`
```Makefile
ARDUINO_FQBN = arduino:avr:nano:cpu=atmega328old
ARDUINO_IFACE = /dev/ttyUSB0
BAUD_RATE = 1000000
```
Alternatively you can invoke make with these parameters to override them:
```bash
make arduino_upload ARDUINO_FQBN="arduino:avr:uno" ARDUINO_IFACE="/dev/ttyUSB1" BAUD_RATE="500000"
```
Obviously if you change the Baud Rate you will need to (re)build `viper_loader`
with the same value.

### Wiring

The Viper GC parallel module only uses uses a few of the parallel interface pins
so you will only require 9 wires to get it working. The Arduino uses 5V logic
levels just like a parallel port and resistors are present on the parallel
module so you can run direct wires.

DB-25 (Name) | Arduino Nano
------------:|:-------------
 2  (D0)     |  Digital D2
 3  (D1)     |  Digital D3
 4  (D2)     |  Digital D4
 5  (D3)     |  Digital D5
 6  (D4)     |  Digital D6
 7  (D5)     |  Digital D7
13  (SELECT) |  Digital D8
15  (ERROR)  |  Digital D9
25  (GND)    |  GND

# Unlicense

>This is free and unencumbered software released into the public domain.
>
>Anyone is free to copy, modify, publish, use, compile, sell, or
>distribute this software, either in source code form or as a compiled
>binary, for any purpose, commercial or non-commercial, and by any
>means.
>
>In jurisdictions that recognize copyright laws, the author or authors
>of this software dedicate any and all copyright interest in the
>software to the public domain. We make this dedication for the benefit
>of the public at large and to the detriment of our heirs and
>successors. We intend this dedication to be an overt act of
>relinquishment in perpetuity of all present and future rights to this
>software under copyright law.
>
>THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
>EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
>MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
>IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
>OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
>ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
>OTHER DEALINGS IN THE SOFTWARE.
>
>For more information, please refer to <https://unlicense.org>
