
/*
  Created: September 2012
  by ihsan Kehribar <ihsan@kehribar.me>

  Permission is hereby granted, free of charge, to any person obtaining a copy of
  this software and associated documentation files (the "Software"), to deal in
  the Software without restriction, including without limitation the rights to
  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
  of the Software, and to permit persons to whom the Software is furnished to do
  so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

/***************************************************************/
/* See the micronucleus_lib.h for the function descriptions/comments */
/***************************************************************/
#include "micronucleus_lib.h"
#include "littleWire_util.h"

micronucleus* micronucleus_connect() {
  micronucleus *nucleus = NULL;
  struct usb_bus *busses;

  // intialise usb and find micronucleus device
  usb_init();
  usb_find_busses();
  usb_find_devices();

  busses = usb_get_busses();
  struct usb_bus *bus;
  for (bus = busses; bus; bus = bus->next) {
    struct usb_device *dev;

    for (dev = bus->devices; dev; dev = dev->next) {
      /* Check if this device is a micronucleus */
      if (dev->descriptor.idVendor == MICRONUCLEUS_VENDOR_ID && dev->descriptor.idProduct == MICRONUCLEUS_PRODUCT_ID)  {
        nucleus = malloc(sizeof(micronucleus));
        nucleus->version.major = (dev->descriptor.bcdDevice >> 8) & 0xFF;
        nucleus->version.minor = dev->descriptor.bcdDevice & 0xFF;

fprintf( stderr, "%d %d\n", nucleus->version.major, nucleus->version.minor );

        if (nucleus->version.major > MICRONUCLEUS_MAX_MAJOR_VERSION) {
          fprintf(stderr, "Warning: device with unknown new version of Micronucleus detected.\n");
          fprintf(stderr, "This tool doesn't know how to upload to this new device. Updates may be available.\n");
          fprintf(stderr, "Device reports version as: %d.%d\n", nucleus->version.major, nucleus->version.minor);
          return NULL;
        }

        nucleus->device = usb_open(dev);

        // get nucleus info
        unsigned char buffer[4];
        int res = usb_control_msg(nucleus->device, 0xC0, 0, 0, 0, buffer, 4, MICRONUCLEUS_USB_TIMEOUT);
        assert(res >= 4);

        nucleus->flash_size = (buffer[0]<<8) + buffer[1];
        nucleus->page_size = buffer[2];
        nucleus->pages = (nucleus->flash_size / nucleus->page_size);
        if (nucleus->pages * nucleus->page_size < nucleus->flash_size) nucleus->pages += 1;
        nucleus->write_sleep = buffer[3];
        nucleus->erase_sleep = nucleus->write_sleep * nucleus->pages;
      }
    }
  }

  return nucleus;
}

int micronucleus_eraseFlash(micronucleus* deviceHandle, micronucleus_callback progress) {
  int res;
  res = usb_control_msg(deviceHandle->device, 0xC0, 2, 0, 0, NULL, 0, MICRONUCLEUS_USB_TIMEOUT);

  // give microcontroller enough time to erase all writable pages and come back online
  float i = 0;
  while (i < 1.0) {
    // update progress callback if one was supplied
    if (progress) progress(i);

    delay(((float) deviceHandle->erase_sleep) / 100.0f);
    i += 0.01;
  }

  /* Under Linux, the erase process is often aborted with errors such as:
   usbfs: USBDEVFS_CONTROL failed cmd micronucleus rqt 192 rq 2 len 0 ret -84
   This seems to be because the erase is taking long enough that the device
   is disconnecting and reconnecting.  Under Windows, micronucleus can see this
   and automatically reconnects prior to uploading the program.  To get the
   the same functionality, we must flag this state (the "-84" error result) by
   converting the return to -2 for the upper layer.

   On Mac OS a common error is -34 = epipe, but adding it to this list causes:
   Assertion failed: (res >= 4), function micronucleus_connect, file library/micronucleus_lib.c, line 63.
  */
  if (res == -5 || res == -34 || res == -84) {
    if (res = -34) {
      usb_close(deviceHandle->device);
      deviceHandle->device = NULL;
    }

    return 1; // recoverable errors
  } else {
    return res;
  }
}

int micronucleus_writeFlash(micronucleus* deviceHandle, unsigned int program_size, unsigned char* program, micronucleus_callback prog) {
  unsigned char page_length = deviceHandle->page_size;
  unsigned char page_buffer[page_length];
  unsigned int  address; // overall flash memory address
  unsigned int  page_address; // address within this page when copying buffer
  unsigned int  res;
  unsigned int  userReset;
  
  for (address = 0; address < deviceHandle->flash_size; address += deviceHandle->page_size) {
    unsigned char unused = 1;
  
    // work around a bug in older bootloader versions
    if (deviceHandle->version.major == 1 && deviceHandle->version.minor <= 2
        && address / deviceHandle->page_size == deviceHandle->pages - 1) {
      page_length = deviceHandle->flash_size % deviceHandle->page_size;
    }

    // copy in bytes from user program
    for (page_address = 0; page_address < page_length; page_address += 1) {
      if (address + page_address > program_size) {
        page_buffer[page_address] = 0xFF; // pad out remainder with unprogrammed bytes
      } else {
          unused = 0;
        page_buffer[page_address] = program[address + page_address]; // load from user program
      }
    }

    // later versions leave it to us to put rjmp to user code at end of flash
    if ( deviceHandle->version.major >= 2 )
    {
      if ( address == 0 ) 
        // save user reset vector (bootloader will patch with its vector)
        userReset = page_buffer [1] * 0x100 + page_buffer [0];
      
      if ( address >= deviceHandle->flash_size - deviceHandle->page_size )
      {
        // move user reset vector to end of last page
        unsigned user_reset_addr = deviceHandle->flash_size;
        unsigned data = (userReset + 0x1000 - user_reset_addr/2) & ~0x1000;
        
        page_buffer [user_reset_addr - address + 0] = data >> 0 & 0xff;
        page_buffer [user_reset_addr - address + 1] = data >> 8 & 0xff;
        unused = 0;
      }
    }
    
    // always write last page so bootloader can patch it if necessary
    if ( address >= deviceHandle->flash_size - deviceHandle->page_size )
      unused = 0;
    
    res = page_length;
    if ( !unused ) // skip unused pages
    {
      // start
      res = usb_control_msg(deviceHandle->device, 0xC0, 1, 0, address,
          NULL, 0, MICRONUCLEUS_USB_TIMEOUT );
      if (res != 0) return -1;
      
      int offset;
      for ( offset = 0; offset < deviceHandle->page_size; offset += 4 )
      {
        int data0 = page_buffer [offset + 0] +
                    page_buffer [offset + 1] * 0x100;
        int data1 = page_buffer [offset + 2] +
                    page_buffer [offset + 3] * 0x100;
        res = usb_control_msg(deviceHandle->device, 0xC0, 3, data0, data1,
            NULL, 0, MICRONUCLEUS_USB_TIMEOUT );
        if (res != 0) return -1;
      }
      res = page_length;
    }
    
    // call progress update callback if that's a thing
    if (prog) prog(((float) address) / ((float) deviceHandle->flash_size));

    if ( !unused )
    {
      // give microcontroller enough time to write this page and come back online
      delay(deviceHandle->write_sleep);
    }
    
    if (res != page_length) return -1;
  }

  // call progress update callback with completion status
  if (prog) prog(1.0);

  return 0;
}

int micronucleus_startApp(micronucleus* deviceHandle) {
  int res;
  res = usb_control_msg(deviceHandle->device, 0xC0, 4, 0, 0, NULL, 0, MICRONUCLEUS_USB_TIMEOUT);

  if(res!=0)
    return -1;
  else
    return 0;
}



