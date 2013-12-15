// Compact USBASP-compatible bootloader for AVR

/*  License: GNU GPL v2 (see License.txt)
Copyright (c) 2007 Christian Starkjohann
Copyright (c) 2007 OBJECTIVE DEVELOPMENT Software GmbH
Copyright (c) 2012 Stephan Baerwolf
Copyright (c) 2012 Louis Beaudoin (USBaspLoader-tiny85)
Copyright (c) 2012 Jenna Fox
Copyright (c) 2013 Shay Green */

#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/boot.h>
#include <avr/power.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/delay.h>

// how many milliseconds should host wait till it sends another erase or write?
// needs to be above 4.5 (and a whole integer) as avr freezes for 4.5ms
#define MICRONUCLEUS_WRITE_SLEEP 8

#define PROGMEM_SIZE BOOTLOADER_ADDRESS

#if BOOTLOADER_ADDRESS % SPM_PAGESIZE != 0
	#error "BOOTLOADER_ADDRESS must be a multiple of page size"
#endif

#define USER_RESET_ADDR (BOOTLOADER_ADDRESS - 2)

#define RESET_VECTOR_ADDR 0

static void leaveBootloader( void ) __attribute__((noreturn)); // optimization

#include "bootloaderconfig.h"
#include "usbdrv/usbdrv.h"

enum { flag_run   = 1<<0 };
enum { flag_write = 1<<1 };
enum { flag_erase = 1<<2 };

register uchar  globalFlags asm( "r3" ); // register saves 14 bytes
static unsigned currentAddress;

#ifndef boot_page_fill_clear
#define boot_page_fill_clear()                   \
(__extension__({                                 \
    __asm__ __volatile__                         \
    (                                            \
        "sts %0, %1\n\t"                         \
        "spm\n\t"                                \
        :                                        \
        : "i" (_SFR_MEM_ADDR(__SPM_REG)),        \
          "r" ((uint8_t)(__BOOT_PAGE_FILL | (1 << CTPB)))     \
    );                                           \
}))
#endif

static void write_flash( void )
{
	boot_page_write( currentAddress - 2 );
}

static void erase_flash( void )
{
	unsigned addr = BOOTLOADER_ADDRESS;
	do {
        addr -= SPM_PAGESIZE;
        boot_page_erase( addr );
        wdt_reset();
	}
	while ( addr );
}

uchar usbFunctionSetup( uchar data [8] )
{
	const usbRequest_t* rq = (const usbRequest_t*) data;
	
	static uchar replyBuffer [4] = {
		PROGMEM_SIZE >> 8 & 0xff,
		PROGMEM_SIZE      & 0xff,
		SPM_PAGESIZE,
		MICRONUCLEUS_WRITE_SLEEP
	};
	
	if ( rq->bRequest == 0 ) // device info
	{
		usbMsgPtr = (usbMsgPtr_t) replyBuffer;
		return sizeof replyBuffer;
	}
	else if ( rq->bRequest == 1 ) // write page
	{
		currentAddress = rq->wIndex.word;
		return USB_NO_MSG; // hands off work to usbFunctionWrite
	}
	else if ( rq->bRequest == 2 ) // erase device
	{
		globalFlags |= flag_erase;
	}
	else // exit
	{
		globalFlags |= flag_run;
	}
	
	return 0;
}

// Called multiple times by usbdrv with a few bytes at a time of the page
uchar usbFunctionWrite( uchar* buf, uchar len )
{
	do
	{
		unsigned data = *(uint16_t*) buf;
		buf += 2;
		
		#if PATCH_RESET
			static unsigned userReset;
			
			if ( currentAddress == RESET_VECTOR_ADDR )
			{
				// Save app's reset vector and replace with ours
				userReset = data;
				data = BOOTLOADER_ADDRESS/2 - 1 + 0xc000; // rjmp BOOTLOADER_ADDR
			}
			
			if ( currentAddress == USER_RESET_ADDR )
			{
				// Relocate app's reset rjmp and adjust offset for new address
				data = (userReset + 0x1000 - USER_RESET_ADDR/2) & ~0x1000;
			}
		#endif
		
		if ( currentAddress >= BOOTLOADER_ADDRESS )
			break;
		
		boot_page_fill( currentAddress, data );
		currentAddress += 2;
	}
	while ( len -= 2 );
	
	if ( currentAddress % SPM_PAGESIZE != 0 )
		return 0; // not done; tell USB we need more
	
	globalFlags |= flag_write;
	return 1; // tell USB we've received everything
}

static void leaveBootloader( void )
{
	usbDeviceDisconnect();

	USB_INTR_ENABLE = 0;
	USB_INTR_CFG    = 0; // also reset config bits
	
	bootLoaderExit();
    
	typedef void (*vector_t)( void ) __attribute__((noreturn));
	vector_t user_reset = (vector_t) (USER_RESET_ADDR / 2);
	user_reset();
}

static void initHardware( void )
{
	// Clear cause-of-reset flags and try to disable WDT
	MCUCR = 0; // WDRF must be clear or WDT can't be disabled on some MCUs
	WDTCR = 1<<WDCE | 1<<WDE;
	WDTCR = 1<<WDP2 | 1<<WDP1 | 1<<WDP0; // maximum timeout in case WDT is fused on
	
	// Required in case user code jumps to bootloader and leaves temporary buffer
	// partly filled
	boot_page_fill_clear();
	
	usbInit();
	
	// Force USB re-enumerate so host sees us
	usbDeviceDisconnect();
	_delay_ms( 260 );
	usbDeviceConnect();
}

ISR(USB_INTR_VECTOR);

static void wait_usb_interrupt( void )
{
	// Clear any stale pending interrupt, then wait for interrupt flag
	USB_INTR_PENDING = 1<<USB_INTR_PENDING_BIT;
	while ( !(USB_INTR_PENDING & (1<<USB_INTR_PENDING_BIT)) )
		wdt_reset();
	
	for ( ;; )
	{
		// Vector interrupt manually
		USB_INTR_PENDING = 1<<USB_INTR_PENDING_BIT;
		USB_INTR_VECTOR();
		
		// Wait a little while longer in case another one comes
		uchar n = 20;
		do {
			if ( !--n )
				goto handled;
		}
		while ( !(USB_INTR_PENDING & (1<<USB_INTR_PENDING_BIT)) );
	}
handled:
	
	usbPoll();
}

#if AUTO_OSCCAL
static volatile uchar osc_not_calibrated = 1;
#endif

extern uchar usbCurrentTok;

int main( void ) __attribute__((noreturn,OS_main)); // optimization
int main( void )
{
	// TODO: default implementation is bloated
	//clock_prescale_set( clock_div_1 );
	
	// Allow user to see registers before any disruption
	bootLoaderInit();
	
	initHardware(); // gives time for jumper pull-ups to stabilize
	
	#if AUTO_OSCCAL
		// Must be done separately since reset checking is done in usbPoll()
		// and wait_usb_interrupt() doesn't call it repeatedly.
		while ( osc_not_calibrated )
			usbPoll();
	#endif
	
	while ( bootLoaderCondition() )
	{
		globalFlags = 0;
		
		// Run USB until we have some action to take and that transaction is complete
		do {
			wait_usb_interrupt();
		}
		while ( !globalFlags || usbCurrentTok != 0 );
		
		// Now we can ignore USB until our host program makes another request
		
		if ( globalFlags & flag_erase )
			erase_flash();
		
		if ( globalFlags & flag_write )
			write_flash();
	
		if ( globalFlags & flag_run )
			break;
	}
	
	leaveBootloader();
}

// prevent useless warning
USB_PUBLIC usbMsgLen_t usbFunctionDescriptor(struct usbRequest *rq) { return 0; }

#include "usbdrv/usbdrv.c" // optimization: helps to have source in same file
