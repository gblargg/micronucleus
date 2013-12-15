# Settings for attiny85 using internal RC oscillator
F_CPU      = 16500000
DEVICE     = attiny85
PROGRAMMER = -c usbasp
BOOTLOADER_ADDRESS = 0x1800
FUSEOPT = -U lfuse:w:0xe1:m -U hfuse:w:0xdd:m -U efuse:w:0xfe:m
