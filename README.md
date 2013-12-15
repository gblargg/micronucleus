I'm working on improving the micronucleus bootloader for attiny. My main goal is clear code. I'm also working on smaller code.

The main change is polled USB, and the simplifications is brings in not having to patch the interrupt vector and pass it through.

The code is all configured for an attiny85 using internal RC at 16.5 MHz with automatic OSCCAL calibration based on USB, and USB connected with D+ on pin 3 and D- on pin 2.

Using avr-gcc (GCC) 4.5.3, I get 1798 bytes of code (200 less disabling OSCCAL calibration, ugh).

The only change to usbdrv is USB_POLLED turning the RETI into RET.


micronucleus
------------
* I based this on a USBaspLoader rewrite I'm working on, though it's pretty similar to the current micronucleus.

* A central goal is code clarity, to allow getting the fundamentals working really well. The large number of options in the main version overwhelm me.

* All received USB data's checksum is verified before it's operated on, including bytes in the protocol headers.

* Reset vector can be patched in bootloader (adds 46 bytes) or on host. In latter case major version is bumped to 2 and host handles this differently. Since CRC is now checked, only bugs in host program can cause wrong reset vector to be written. I'm including this option mainly as a proof-of-concept to evaluate.

* Flash erase is simplified to just erasing everything below the bootloader, from end to beginning.

* As far as I can tell, it's still robust to interruption.
- Interruption before erasure is complete leaves reset vector to bootloader in place.
- Interruption after erasure leaves effectively NOPs before bootloader.
- Interruption after first page is programmed leaves reset vector to bootloader.

* I tried lowering MICRONUCLEUS_WRITE_SLEEP from 8ms to 5ms. It dropped writing time from 2.95s to 2.49s, a 17% improvement. Note that with the unused flash skipping optimization, this gain would be much smaller. 8ms seems less risky so I've left it there.

* As for further speedups, flash erasure could skip pages already erased, at the very least to extend flash life. The bootloader could count the number of non-erased flash pages first thing, and append this to the device info reply. Then the host could use this to calculate the erasure time and not have to wait so long. The count wouldn't need to be sparse, just the total page count - count of unused ones above the user program. Maybe not worth the small speedup of only less than a second.

* I incorporated the modified crt1.S and removed the unneeded vectors from it other than reset. There was some "zerovectors" section I removed, not sure what that was for.


Commandline
-----------
* Supports reset vector patching in the tool rather than in the bootloader.

* Supports just re-running user program, without uploading anything. Useful if bootloader is running and you don't have a way to exit it easily.

* Optimizes out writing of empty flash pages. This cut writing time from 2.95 to 1.36 seconds. Still forces writing last one so bootloader can patch it if desired.



USB improvement
---------------
I logged some globals after each USB interrupt to figure out when USB could be interrupted. Format of logs:

	TCNT0 usbCurrentTok usbRxToken usbTxLen

TCNT0 was running at 11719Hz

Some hex values from usbdrv:

	2d = USBPID_SETUP
	e1 = USBPID_OUT
	5a = USBPID_NAK

For each of the logs, after those USB events I could have the AVR ignore USB for as long as it wanted without getting host errors, as long as the host hadn't sent any new requests.

Device info (0):

	9d 2d 2d 08
	bc 00 2d 5a
	d3 e1 2d 5a

Erase device, exit/run program:

	5d 2d 2d 04
	74 00 2d 5a
	
	Only two interrupts. This command doesn't send any reply, whereas device info sends 4 bytes back.
	
Write page (1):

	31 2d 2d 5a
	49 e1 e1 5a
	60 e1 e1 5a
	78 e1 e1 5a
	8f e1 e1 5a
	a7 e1 e1 5a
	be e1 e1 5a
	d6 e1 e1 5a
	ed e1 e1 04
	04 00 e1 5a

	Writing the page takes 18ms just for the transfer (0x104-0x31 = 211 = 0.018s).

Sicne we don't need to know when the device info command ends, we just watch for usbCurrentTok==0 to signal the end of a transaction.

-- 
Shay Green <gblargg@gmail.com>
