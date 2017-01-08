/* main.cc - the entry point for the kernel */
/* Copyright (C) 2013 Goswin von Brederlow <goswin-v-b@web.de>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <stdint.h>
#include <archinfo.h>
#include <uart.h>
#include <kprintf.h>
#include <atag.h>

extern "C" {
    // kernel_main gets called from boot.S. Declaring it extern "C" avoid
    // having to deal with the C++ name mangling.
    void kernel_main(uint32_t r0, uint32_t r1, const Header *atags);

    // fake raise function
    void raise(){}
}

typedef void (*constructorFunc)(void);

// this first static constructor function
extern "C" constructorFunc _init_array_start[];

// the next pointer after the last static constructor
extern "C" constructorFunc _init_array_end[];

// void implementations of the C++ ABI required functions
extern "C" void __aeabi_unwind_cpp_pr0(void)
{
}

extern "C" void __aeabi_atexit(void)
{
}

extern "C" void __dso_handle(void)
{
}

extern "C" void __cxa_pure_virtual(void)
{
	while(1) { ; }
}

#define LOADER_ADDR 0x2000000

const char hello[] = "\r\nRaspbootin V1.1\r\n";
const char halting[] = "\r\n*** system halting ***";

typedef void (*entry_fn)(uint32_t r0, uint32_t r1, const Header *atags);

static constexpr ArchInfo arch_infos[ArchInfo::NUM_ARCH_INFOS] = {
    ArchInfo("Raspberry Pi b", 0x20000000, 16, 1),
    ArchInfo("Raspberry Pi b+", 0x20000000, 47, 0),
    ArchInfo("Raspberry Pi b 2", 0x3F000000, 47, 0),
};

const ArchInfo *arch_info;

const char *find(const char *str, const char *token) {
    while(*str) {
	const char *p = str;
	const char *q = token;
	// as long as str matches token
	while(*p && *p == *q) {
	    // keep comparing
	    ++p;
	    ++q;
	}
	if (*q == 0) return str; // found token
	// token not found, try again
	++str;
    }

    // end of string, nothing found
    return NULL;
}

// kernel main function, it all begins here
void kernel_main(uint32_t r0, uint32_t r1, const Header *atags) {
    constructorFunc *fn_init = _init_array_start;
    while(fn_init < _init_array_end) 
    {
        (*fn_init++)();
    }
    // Figure out what kind of Raspberry we are booting on
    // default to basic Raspberry Pi
    arch_info = &arch_infos[ArchInfo::RPI];
    const Cmdline *cmdline = atags->find<Cmdline>();
    if (find(cmdline->cmdline, "bcm2708.disk_led_gpio=47")) {
	arch_info = &arch_infos[ArchInfo::RPIplus];
    }
    if (find(cmdline->cmdline, "bcm2709.disk_led_gpio=47")) {
	arch_info = &arch_infos[ArchInfo::RPI2];
    }
    
    UART::init();
again:
    kprintf("%s", hello);
    kprintf("######################################################################\n");
    kprintf("R0 = %#010x, R1 = %#010x, ATAGs @ %p\n", r0, r1, atags);
    atags->print_all();
    kprintf("Detected '%s'\n", arch_info->model);
    kprintf("######################################################################\n");

    // request kernel by sending 3 breaks
    UART::puts("\x03\x03\x03");

    // get kernel size
    uint32_t size = UART::getc();
    size |= UART::getc() << 8;
    size |= UART::getc() << 16;
    size |= UART::getc() << 24;

    if (0x8000 + size > LOADER_ADDR) {
	UART::puts("SE");
	goto again;
    } else {
	UART::puts("OK");
    }
    
    // get kernel
    uint8_t *kernel = (uint8_t*)0x8000;
    while(size-- > 0) {
	*kernel++ = UART::getc();
    }

    // Kernel is loaded at 0x8000, call it via function pointer
    UART::puts("booting...");
    entry_fn fn = (entry_fn)0x8000;
    fn(r0, r1, atags);

    // fn() should never return. But it might, so make sure we catch it.
    // Wait a bit
    for(volatile int i = 0; i < 10000000; ++i) { }

    // Say goodbye and return to boot.S to halt.
    UART::puts(halting);
}

