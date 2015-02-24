/*******************************************************************************
 * Copyright (c) 2014 IBM Corporation.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors:
 *	IBM Zurich Research Lab - initial API, implementation and documentation
 *******************************************************************************/

#define NEW_CHNL 1

#ifndef _lmic_h_
#define _lmic_h_

#include "oslmic.h"
#include "lorabase.h"

enum { MAX_FRAME_LEN	=  64 };   // Library cap on max frame length
enum { TXCONF_ATTEMPTS	=   8 };   // transmit attempts for confirmed frames
enum { MAX_MISSED_BCNS	=  20 };   // threshold for triggering rejoin requests
enum { MAX_RXSYMS		= 100 };   // stop tracking beacon beyond this

enum { LINK_CHECK_CONT	=   6 };   // continue with this after reported dead link
enum { LINK_CHECK_DEAD	=  12 };   // after this UP frames and no response from NWK assume link is dead
enum { LINK_CHECK_INIT	= -12 };   // UP frame count until we inc datarate

enum { TIME_RESYNC			= 6*128 }; // secs
enum { TXRX_GUARD_ms		=  6000 };  // msecs - don't start TX-RX transaction before beacon
enum { JOIN_GUARD_ms		=  9000 };  // msecs - don't start Join Req/Acc transaction before beacon
enum { TXRX_BCNEXT_secs		=	 2 };  // secs - earliest start after beacon time
enum { RETRY_PERIOD_secs	=	 3 };  // secs - random period for retrying a confirmed send

#if CFG_eu434 // EU434 spectrum ====================================================

	enum { MAX_CHANNELS = 8 };	  // library may not support all 16 channels
	enum { MAX_BANDS	= 4 };

	enum { LIMIT_CHANNELS = (1 << 4) };   // EU434 will never have more channels
	struct band_t {
		uint16_t	 txcap;  // duty cycle limitation: 1/txcap
		int8_t	 txpow;  // maximum TX power
		ostime_t avail;  // channel is blocked until this time
	};
	TYPEDEF_xref2band_t;

#elif CFG_eu868 // EU868 spectrum ====================================================

	enum { MAX_CHANNELS = 8 };	  // library may not support all 16 channels
	enum { MAX_BANDS	= 4 };

	enum { LIMIT_CHANNELS = (1<<4) };   // EU868 will never have more channels
	struct band_t {
		uint16_t	 txcap;  // duty cycle limitation: 1/txcap
		int8_t	 txpow;  // maximum TX power
		ostime_t avail;  // channel is blocked until this time
	};
	TYPEDEF_xref2band_t;

#elif CFG_us915  // US915 spectrum =================================================

	enum { MAX_XCHANNELS = 2 };	  // extra channels in RAM, channels 0-71 are immutable 
	enum { MAX_TXPOW_125kHz = 30 };

#endif // ==========================================================================

// Keep in sync with evdefs.hpp::drChange
enum { DRCHG_SET, DRCHG_NOJACC, DRCHG_NOACK, DRCHG_NOADRACK, DRCHG_NWKCMD };
enum { KEEP_TXPOW = -128 };

struct rxsched_t {
	uint8_t	 dr;
	uint8_t	 intvExp;   // 0..7
	uint8_t	 slot;	  // runs from 0 to 128
	uint8_t	 rxsyms;
	ostime_t rxbase;
	ostime_t rxtime;	// start of next spot
	uint32_t	 freq;
};
TYPEDEF_xref2rxsched_t;


// Information extracted from (last) beacon
enum {
	BCN_NONE	= 0x00,
	BCN_PARTIAL = 0x01,
	BCN_FULL	= 0x02,
	BCN_NODRIFT = 0x04,	// no drift value measured
	BCN_NODDIFF = 0x08,
};  // no differential drift measured yet

struct bcninfo_t {
	rxqu_t   rxq;
	ostime_t txtime;  // time when beacon was sent
	uint8_t	 flags;
	uint32_t	 time;	// GPS time in seconds
	// This part is valid only if flags==BCN_FULL
	uint8_t	 info;	// info field
	int32_t	 lat;
	int32_t	 lon;
};

// purpose of receive window - lmic_t.rxState
enum { RADIO_RST = 0, RADIO_TX = 1, RADIO_RX = 2, RADIO_RXON = 3 };
// Netid values /  lmic_t.netid
enum { NETID_NONE = (int)(~0U), NETID_MASK = 0xFFFFFF };
// MAC operation modes (lmic_t.opmode).
enum { OP_NONE		= 0x0000,
	   OP_SCAN		= 0x0001, // radio scan to find a beacon
	   OP_TRACK		= 0x0002, // track my networks beacon (netid)
	   OP_JOINING	= 0x0004, // device joining in progress (blocks other activities)
	   OP_TXDATA	= 0x0008, // TX user data (buffered in pendTxData)
	   OP_POLL		= 0x0010, // send empty UP frame to ACK confirmed DN/fetch more DN data
	   OP_REJOIN	= 0x0020, // occasionally send JOIN REQUEST
	   OP_SHUTDOWN	= 0x0040, // prevent MAC from doing anything
	   OP_TXRXPEND	= 0x0080, // TX/RX transaction pending
	   OP_RNDTX		= 0x0100, // prevent TX lining up after a beacon
	   OP_PINGINI	= 0x0200, // pingable is initialized and scheduling active
	   OP_PINGABLE	= 0x0400, // we're pingable
	   OP_NEXTCHNL	= 0x0800, // find a new channel
	   OP_LINKDEAD	= 0x1000, // link was reported as dead
};
// TX-RX transaction flags - report back to user
enum {
	TXRX_ACK	= 0x80,   // confirmed UP frame was acked
	TXRX_NACK   = 0x40,   // confirmed UP frame was not acked
	TXRX_NOPORT = 0x20,   // set if a frame with a port was RXed, clr if no frame/no port
	TXRX_DNW1   = 0x01,   // received in 1st DN slot
	TXRX_DNW2   = 0x02,   // received in 2dn DN slot
	TXRX_PING   = 0x04,
}; // received in a scheduled RX slot
// Event types for event callback
enum _ev_t {
	EV_SCAN_TIMEOUT = 1,
	EV_BEACON_FOUND,
	EV_BEACON_MISSED,
	EV_BEACON_TRACKED,
	EV_JOINING,
	EV_JOINED,
	EV_RFU1,
	EV_JOIN_FAILED,
	EV_REJOIN_FAILED,
	EV_TXCOMPLETE,
	EV_LOST_TSYNC,
	EV_RESET,
	EV_RXCOMPLETE,
	EV_LINK_DEAD,
	EV_LINK_ALIVE,
};

typedef enum _ev_t ev_t;

struct lmic_t {
	// Radio settings TX/RX (also accessed by HAL)
	ostime_t	txend;
	ostime_t	rxtime;
	rxqu_t		rxq;
	uint32_t		freq;
	rps_t		rps;
	uint8_t		rxsyms;
	int8_t		txpow;	// dBm

	osjob_t	 osjob;

	// Channel scheduling
#if CFG_eu434
	band_t		bands[MAX_BANDS];
	uint32_t		channelFreq[MAX_CHANNELS];
	uint8_t		channelDrs[MAX_CHANNELS];
	uint16_t		channelMap;
#elif CFG_eu868
	band_t		bands[MAX_BANDS];
	uint32_t		channelFreq[MAX_CHANNELS];
	uint8_t		channelDrs[MAX_CHANNELS];
	uint16_t		channelMap;
#elif CFG_us915
	uint32_t		xchFreq[MAX_XCHANNELS];	// extra channel frequencies (if device is behind a repeater)
	uint8_t		xchDrs[MAX_XCHANNELS];	// extra channel datarate ranges  ---XXX: ditto
	uint16_t		channelMap[(72 + MAX_XCHANNELS + 15) / 16];	// enabled bits
	uint8_t		chRnd;	// channel randomizer
#endif
	uint8_t		txChnl;				// channel for next TX
	uint8_t		globalDutyRate;		// max rate: 1/2^k
	ostime_t	globalDutyAvail;	// time device can send again

	uint32_t		netid;		// current network id (~0 - none)
	uint16_t		opmode;
	uint8_t		upRepeat;	// configured up repeat
	int8_t		adrTxPow;	// ADR adjusted TX power
	uint8_t		datarate;	// current data rate
	uint8_t		errcr;		// error coding rate (used for TX only)
	uint8_t		rejoinCnt;	// adjustment for rejoin datarate
	int16_t		drift;		// last measured drift
	int16_t		lastDriftDiff;
	int16_t		maxDriftDiff;

	uint8_t		pendTxPort;
	uint8_t		pendTxConf;	// confirmed data
	uint8_t		pendTxLen;	// +0x80 = confirmed
	uint8_t		pendTxData[MAX_LEN_PAYLOAD];

	uint16_t		devNonce;	// last generated nonce
	uint8_t		nwkKey[16];	// network session key
	uint8_t		artKey[16];	// application router session key
	devaddr_t   devaddr;
	uint32_t		seqnoDn;	// device level down stream seqno
	uint32_t		seqnoUp;

	uint8_t		dnConf;		// dn frame confirm pending: LORA::FCT_ACK or 0
	int8_t		adrAckReq;	// counter until we reset data rate (0=off)
	uint8_t		adrChanged;

	uint8_t		margin;
	uint8_t	   ladrAns;		// link adr adapt answer pending
	uint8_t		devsAns;	// device status answer pending
	uint8_t		adrEnabled;
	uint8_t		moreData;	// NWK has more data pending
	uint8_t		dutyCapAns;	// have to ACK duty cycle settings
	uint8_t		snchAns;	// answer set new channel
	// 2nd RX window (after up stream)
	uint8_t		dn2Dr;
	uint32_t		dn2Freq;
	uint8_t		dn2Ans;		// 0=no answer pend, 0x80+ACKs

	// Class B state
	uint8_t		missedBcns;		// unable to track last N beacons
	uint8_t		bcninfoTries;	// how often to try (scan mode only)
	uint8_t		pingSetAns;		// answer set cmd and ACK bits
	rxsched_t   ping;			// pingable setup

	// Public part of MAC state
	uint8_t		txCnt;
	uint8_t		txrxFlags;	// transaction flags (TX-RX combo)
	uint8_t		dataBeg;	// 0 or start of data (dataBeg-1 is port)
	uint8_t		dataLen;	// 0 no data or zero length data, >0 byte count of data
	uint8_t		frame[MAX_LEN_FRAME];

	uint8_t		bcnChnl;
	uint8_t		bcnRxsyms;	// 
	ostime_t	bcnRxtime;
	bcninfo_t   bcninfo;	// Last received beacon info
};

DECLARE_LMIC;

void  LMIC_setDrTxpow	(dr_t dr, int8_t txpow);	// set default/start DR/txpow
void  LMIC_setAdrMode	(uint8_t enabled);		// set ADR mode (if mobile turn off)
uint8_t LMIC_startJoining	(void);

void  LMIC_shutdown		(void);
void  LMIC_init			(void);
void  LMIC_reset		(void);
void  LMIC_clrTxData	(void);
void  LMIC_setTxData	(void);
int   LMIC_setTxData2	(uint8_t port, xref2uint8_t data, uint8_t dlen, uint8_t confirmed);
void  LMIC_sendAlive	(void);

uint8_t LMIC_enableTracking	(uint8_t tryBcnInfo);
void  LMIC_disableTracking	(void);

void  LMIC_stopPingable	(void);
void  LMIC_setPingable	(uint8_t intvExp);
void  LMIC_tryRejoin	(void);

#endif // _lmic_h_
