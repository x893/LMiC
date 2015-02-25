#ifndef RadioHead_h
#define RadioHead_h

// Official version numbers are maintained automatically by Makefile:
#define RH_VERSION_MAJOR 1
#define RH_VERSION_MINOR 39

#include <stdint.h>
#include <string.h>

#define RH_HAVE_HARDWARE_SPI
#define PROGMEM
#define memcpy_P memcpy
#define Serial SerialUSB
#define RH_HAVE_SERIAL

#define ATOMIC_BLOCK_START
#define ATOMIC_BLOCK_END

#define YIELD

// This is the address that indicates a broadcast
#define RH_BROADCAST_ADDRESS 0xFF

#endif
