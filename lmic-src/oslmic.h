/*******************************************************************************
 * Copyright (c) 2014 IBM Corporation.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors:
 *    IBM Zurich Research Lab - initial API, implementation and documentation
 *******************************************************************************/

#ifndef _oslmic_h_
#define _oslmic_h_

// Dependencies required for the LoRa MAC in C to run.
// These settings can be adapted to the underlying system.
// You should not, however, change the lmic.[hc]



//================================================================================
//================================================================================
// Target platform as C library
/*
typedef unsigned char		bit_t;
typedef unsigned char		uint8_t;
typedef   signed char		s1_t;
typedef unsigned short		uint16_t;
typedef          short		s2_t;
typedef unsigned int		uint32_t;
typedef          int		int32_t;
typedef unsigned long long	u8_t;
typedef          long long	s8_t;
typedef unsigned int		uint;
typedef const char*			str_t;
*/
#include <string.h>
#include "hal.h"

#define EV(a,b,c) /**/
#define DO_DEVDB(meth,...) /**/

#if !CFG_noassert
	#define ASSERT(cond) if (!(cond)) hal_failed()
#else
	#define ASSERT(cond) /**/
#endif

#define os_clearMem(a,b)   memset(a,0,b)
#define os_copyMem(a,b,c)  memcpy(a,b,c)

typedef     struct osjob_t	osjob_t;
typedef      struct band_t	band_t;
typedef      struct rxqu_t	rxqu_t;
typedef   struct chnldef_t	chnldef_t;
typedef   struct rxsched_t	rxsched_t;
typedef   struct bcninfo_t	bcninfo_t;
typedef    const uint8_t *	xref2cuint8_t;
typedef          uint8_t *	xref2uint8_t;

#define TYPEDEF_xref2rps_t     typedef	rps_t*		xref2rps_t
#define TYPEDEF_xref2rxqu_t    typedef	rxqu_t*		xref2rxqu_t
#define TYPEDEF_xref2rxsched_t typedef	rxsched_t*	xref2rxsched_t
#define TYPEDEF_xref2chnldef_t typedef	chnldef_t*	xref2chnldef_t
#define TYPEDEF_xref2band_t    typedef	band_t*		xref2band_t
#define TYPEDEF_xref2osjob_t   typedef	osjob_t*	xref2osjob_t

#define SIZEOFEXPR(x)		sizeof(x)

#define ON_LMIC_EVENT(ev)	onEvent(ev)
#define DECL_ON_LMIC_EVENT	void onEvent(ev_t e)

extern uint32_t AESAUX[];
extern uint32_t AESKEY[];
#define AESkey			((uint8_t*)AESKEY)
#define AESaux			((uint8_t*)AESAUX)
#define FUNC_ADDR(func)	(&(func))

#define os_getRndU1()	radio_rand1()

#define DEFINE_LMIC		struct lmic_t LMIC
#define DECLARE_LMIC	extern struct lmic_t LMIC

uint8_t radio_rand1		(void);
void radio_init			(void);
void radio_irq_handler	(uint8_t dio);
void os_init			(void);
void os_runloop			(void);

//================================================================================

#ifndef RX_RAMPUP
	#define RX_RAMPUP  (us2osticks(2000))
#endif
#ifndef TX_RAMPUP
	#define TX_RAMPUP  (us2osticks(2000))
#endif

#ifndef OSTICKS_PER_SEC
	#define OSTICKS_PER_SEC		32768
#elif OSTICKS_PER_SEC < 10000 || OSTICKS_PER_SEC > 64516
	#error Illegal OSTICKS_PER_SEC - must be in range [10000:64516]. One tick must be 15.5us .. 100us long.
#endif

typedef int32_t  ostime_t;

#if !HAS_ostick_conv
	#define us2osticks(us)		((ostime_t)( ((uint32_t)(us)  * OSTICKS_PER_SEC) / 1000000))
	#define ms2osticks(ms)		((ostime_t)( ((uint32_t)(ms)  * OSTICKS_PER_SEC)    / 1000))
	#define sec2osticks(sec)	((ostime_t)(  (uint32_t)(sec) * OSTICKS_PER_SEC))
	#define osticks2ms(os)		((int32_t)(((os)*(uint32_t)1000    ) / OSTICKS_PER_SEC))
	#define osticks2us(os)		((int32_t)(((os)*(uint32_t)1000000 ) / OSTICKS_PER_SEC))
	// Special versions
	#define us2osticksCeil(us)	((ostime_t)( ((uint32_t)(us) * OSTICKS_PER_SEC + 999999) / 1000000))
	#define us2osticksRound(us)	((ostime_t)( ((uint32_t)(us) * OSTICKS_PER_SEC + 500000) / 1000000))
	#define ms2osticksCeil(ms)	((ostime_t)( ((uint32_t)(ms) * OSTICKS_PER_SEC + 999) / 1000))
	#define ms2osticksRound(ms)	((ostime_t)( ((uint32_t)(ms) * OSTICKS_PER_SEC + 500) / 1000))
#endif


struct osjob_t; // fwd decl.
typedef void (*osjobcb_t) (struct osjob_t*);
struct osjob_t
{
	struct osjob_t *	next;
			ostime_t	deadline;
			osjobcb_t	func;
};
TYPEDEF_xref2osjob_t;

#if !HAS_os_calls
	#ifndef os_getDevKey
		void os_getDevKey (xref2uint8_t buf);
	#endif
	#ifndef os_getArtEui
		void os_getArtEui (xref2uint8_t buf);
	#endif
	#ifndef os_getDevEui
		void os_getDevEui (xref2uint8_t buf);
	#endif
	#ifndef os_setCallback
		void os_setCallback (xref2osjob_t job, osjobcb_t cb);
	#endif
	#ifndef os_setTimedCallback
		void os_setTimedCallback (xref2osjob_t job, ostime_t time, osjobcb_t cb);
	#endif
	#ifndef os_clearCallback
		void os_clearCallback (xref2osjob_t job);
	#endif
	#ifndef os_getTime
		ostime_t os_getTime (void);
	#endif
	#ifndef os_getTimeSecs
		uint32_t os_getTimeSecs (void);
	#endif
	#ifndef os_radio
		void os_radio (uint8_t mode);
	#endif
	#ifndef os_getBattLevel
		uint8_t os_getBattLevel (void);
	#endif

	#ifndef os_rlsbf4
		//! Read 32-bit quantity from given pointer in little endian byte order.
		uint32_t os_rlsbf4 (xref2cuint8_t buf);
	#endif
	#ifndef os_wlsbf4
		//! Write 32-bit quntity into buffer in little endian byte order.
		void os_wlsbf4 (xref2uint8_t buf, uint32_t value);
	#endif
	#ifndef os_rmsbf4
		//! Read 32-bit quantity from given pointer in big endian byte order.
		uint32_t os_rmsbf4 (xref2cuint8_t buf);
	#endif
	#ifndef os_wmsbf4
		//! Write 32-bit quntity into buffer in big endian byte order.
		void os_wmsbf4 (xref2uint8_t buf, uint32_t value);
	#endif
	#ifndef os_rlsbf2
		//! Read 16-bit quantity from given pointer in little endian byte order.
		uint16_t os_rlsbf2 (xref2cuint8_t buf);
	#endif
	#ifndef os_wlsbf2
		//! Write 16-bit quntity into buffer in little endian byte order.
		void os_wlsbf2 (xref2uint8_t buf, uint16_t value);
	#endif

	//! Get random number (default impl for uint16_t).
	#ifndef os_getRndU2
		#define os_getRndU2() ((uint16_t)((os_getRndU1() << 8) | os_getRndU1()))
	#endif
	#ifndef os_crc16
		uint16_t os_crc16 (xref2uint8_t d, uint16_t len);
	#endif

#endif // !HAS_os_calls

// ======================================================================
// AES support
// !!Keep in sync with lorabase.hpp!!

#ifndef AES_ENC  // if AES_ENC is defined as macro all other values must be too
	#define AES_ENC       0x00 
	#define AES_DEC       0x01
	#define AES_MIC       0x02
	#define AES_CTR       0x04
	#define AES_MICNOAUX  0x08
#endif
#ifndef AESkey  // if AESkey is defined as macro all other values must be too
	extern xref2uint8_t AESkey;
	extern xref2uint8_t AESaux;
#endif
#ifndef os_aes
	uint32_t os_aes (uint8_t mode, xref2uint8_t buf, uint16_t len);
#endif

#endif // _oslmic_h_
