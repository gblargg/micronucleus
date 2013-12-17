#ifndef BOOTLOADERCONFIG_H
#define BOOTLOADERCONFIG_H

// Which pins USB is connected to
#define USB_CFG_IOPORTNAME  B
#define USB_CFG_DMINUS_BIT  3
#define USB_CFG_DPLUS_BIT   4

// Automatically enabled for 12.8MHz and 16.5MHz; uncomment to force
// on or off
//#define AUTO_OSCCAL 1

// Uncomment to enable version 2 protocol that has host do part of reset
// vector patching. Saves 36 bytes.
#define MICRONUCLEUS_VERSION_MAJOR 2

// Uncomment to delay rather than erase/write flash, so USB timing can be tested
// without wearing out device
//#define SIMULATE_FLASH 1

#ifndef __ASSEMBLER__

// Exits bootloader, calls bootLoaderExit(), then runs user program
static void leaveBootloader( void );

// Customize these as desired

// Called before anything else. OK to call leaveBootloader().
static inline void bootLoaderInit(void)
{
}

// Called just before running user program
static inline void bootLoaderExit(void)
{
}

// Bootloader is run only as long as this returns non-zero
#define bootLoaderCondition()   1

#endif
#endif
