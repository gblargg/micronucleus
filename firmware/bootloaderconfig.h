#ifndef BOOTLOADERCONFIG_H
#define BOOTLOADERCONFIG_H

// Which pins USB is connected to
#define USB_CFG_IOPORTNAME  B
#define USB_CFG_DMINUS_BIT  3
#define USB_CFG_DPLUS_BIT   4

// Automatically enabled for 12.8MHz and 16.5MHz; uncomment to force
// on or off
//#define AUTO_OSCCAL 1

// Do reset patching in bootloader rather than letting host do it.
// Adds 46 bytes. Disabling requires special version of host client.
#define PATCH_RESET 1

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
