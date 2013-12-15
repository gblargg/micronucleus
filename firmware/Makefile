include bootloaderconfig.inc

SOURCES += usbdrv/usbdrvasm.S
SOURCES += usbdrv/oddebug.c
SOURCES += main.c

CFLAGS  += -Wall
CFLAGS  += -DBOOTLOADER_ADDRESS=$(BOOTLOADER_ADDRESS)
LDFLAGS += -Wl,--section-start=.text=$(BOOTLOADER_ADDRESS)

# Optimizations

# saves thousands of bytes
CFLAGS  += -Os

# saves 0 bytes
CFLAGS  += -ffunction-sections -fdata-sections -fpack-struct

# saves 2 bytes
LDFLAGS += -Wl,--relax,--gc-sections

# saves 30 bytes
CFLAGS  += -fno-move-loop-invariants -fno-tree-scev-cprop -fno-inline-small-functions

# saves 30 bytes
SOURCES += crt1.S
CFLAGS  += -nostartfiles

AVRDUDE = avrdude $(PROGRAMMER) -p $(DEVICE)

all: hex
	@avr-size obj/main.bin
	@avr-objdump -d obj/main.bin > obj/main.lss

settings:
	@echo BOOTLOADER_ADDRESS $(BOOTLOADER_ADDRESS)
	@echo FUSEOPT $(FUSEOPT)

hex:
	@mkdir -p obj
	@avr-gcc -mmcu=$(DEVICE) -DF_CPU=$(F_CPU) $(CFLAGS) $(LDFLAGS) $(LDFLAGS) \
		-o obj/main.bin -I. $(SOURCES)
	@avr-objcopy -R .eeprom -R .fuse -R .lock -R .signature -O ihex obj/main.bin obj/main.hex

flash: hex
	$(AVRDUDE) -U flash:w:obj/main.hex

fuse:
	$(AVRDUDE) $(FUSEOPT)

clean:
	@-rm obj/*
