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

#define PROGMEM_SIZE USER_RESET_ADDR

#if BOOTLOADER_ADDRESS % SPM_PAGESIZE != 0
	#error "BOOTLOADER_ADDRESS must be a multiple of page size"
#endif

#define USER_RESET_ADDR (BOOTLOADER_ADDRESS - 2)

#define RESET_VECTOR_ADDR 0

static void leaveBootloader( void ) __attribute__((noreturn)); // optimization

#include "bootloaderconfig.h"
#include "usbdrv/usbdrv.h"

enum { cmd_info    = 0 };
enum { cmd_write   = 1 };
enum { cmd_erase   = 2 };
enum { cmd_fill    = 3 };
enum { cmd_exit    = 4 };
enum { cmd_filled  = 0x80 };
enum { cmd_written  = 0x81 };
static uchar    prevCommand;
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
	if ( currentAddress - 2 < BOOTLOADER_ADDRESS )
	{
		boot_page_write( currentAddress - 2 );
		prevCommand = cmd_written;
	}
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

static void fill_flash( unsigned data )
{
	enum { rjmp_bootloader = BOOTLOADER_ADDRESS/2 - 1 + 0xc000 };
	
	if ( currentAddress == RESET_VECTOR_ADDR )
		data = rjmp_bootloader;
	
	boot_page_fill( currentAddress, data );
	currentAddress += 2;
}

uchar usbFunctionSetup( uchar data [8] )
{
	const usbRequest_t* rq = (const usbRequest_t*) data;
	
	static const uchar replyBuffer [4] = {
		PROGMEM_SIZE >> 8 & 0xff,
		PROGMEM_SIZE      & 0xff,
		SPM_PAGESIZE,
		MICRONUCLEUS_WRITE_SLEEP
	};
	
	uchar result = 0;
	uchar bRequest = rq->bRequest;
	
	if ( bRequest == cmd_info )
	{
		usbMsgPtr = (usbMsgPtr_t) replyBuffer;
		result = sizeof replyBuffer;
	}
	else if ( bRequest == cmd_write )
	{
		currentAddress = rq->wIndex.word & ~(SPM_PAGESIZE - 1);
		if ( prevCommand != cmd_written )
			currentAddress = 0;
		
		// Required in case page is already partially filled
		boot_page_fill_clear();
	}
	else if ( bRequest == cmd_fill )
	{
		fill_flash( rq->wValue.word );
		fill_flash( rq->wIndex.word );
		if ( currentAddress % SPM_PAGESIZE == 0 )
			bRequest = cmd_filled;
	}
	
	prevCommand = bRequest;
	return result;
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
	
	usbInit();
	
	// Force USB re-enumerate so host sees us
	usbDeviceDisconnect();
	_delay_ms( 260 );
	usbDeviceConnect();
}

ISR(USB_INTR_VECTOR);

#if AUTO_OSCCAL
static uchar osc_not_calibrated = 1;
#endif

static void wait_usb_interrupt( void )
{
	#if AUTO_OSCCAL
		// don't wait for interrupt until calibrated
		if ( osc_not_calibrated )
			goto handled;
	#endif
	
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
		uchar n = 250; // about 90us timeout
		do {
			if ( !--n )
				goto handled;
		}
		while ( !(USB_INTR_PENDING & (1<<USB_INTR_PENDING_BIT)) );
	}
handled:
	
	usbPoll();
}

// prevent useless warning
USB_PUBLIC usbMsgLen_t usbFunctionDescriptor(struct usbRequest *rq) { return 0; }

#include "usbdrv/usbdrv.c" // optimization: helps to have source in same file

int main( void ) __attribute__((noreturn,OS_main)); // optimization
int main( void )
{
	// TODO: default implementation is bloated
	//clock_prescale_set( clock_div_1 );
	
	// Allow user to see registers before any disruption
	bootLoaderInit();
	
	initHardware(); // gives time for jumper pull-ups to stabilize
	
	while ( bootLoaderCondition() )
	{
		// Run USB until we have some action to take and that transaction is complete
		uchar prevTxLen;
		do {
			prevTxLen = usbTxLen;
			wait_usb_interrupt();
		}
		while ( !(usbTxLen == USBPID_NAK && prevTxLen != USBPID_NAK) );
		
		// Stops once we have a command and we've just transmitted the final reply
		// back to host
		
		// Now we can ignore USB until our host program makes another request
		
		if ( prevCommand == cmd_erase )
			erase_flash();
		else if ( prevCommand == cmd_filled )
			write_flash();
		else if ( prevCommand == cmd_exit )
			break;
	}
	
	leaveBootloader();
}
