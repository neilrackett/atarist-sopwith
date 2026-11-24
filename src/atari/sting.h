/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// The parts of the STinG TCP/IP stack's client interface that network
// games use, declared for this program, which is built by gcc with
// 32-bit ints.
//
// STinG's functions follow Pure C's cdecl convention: arguments on the
// stack at their own sizes, so a 16-bit int takes two bytes, and the
// caller removes them; D0, D1, D2, A0 and A1 may be changed; the result,
// a pointer as much as an integer, comes back in D0. gcc, with 32-bit ints,
// widens every argument to four bytes, so these functions are never
// called directly: tcpcomm_sting.c lays out each call's arguments as
// Pure C would and calls through one small assembly thunk.
//
// The layouts are STinG 1.26's (TRANSPRT.H, PORT.H, STX.H); everything
// here is two-byte aligned, as the 68000 and Pure C both have it.

#ifndef __STING_H__
#define __STING_H__

#include <stdint.h>

// The cookie STinG (and STiK before it) installs: the driver list.
#define STING_COOKIE      0x5354694BL   /* 'STiK' */
#define STING_MAGIC       "STiKmagic"

// Names of the two tables get_dftab() returns.
#define STING_TRANSPORT   "TRANSPORT_TCPIP"
#define STING_MODULE      "MODULE_LAYER"

typedef struct {
	char magic[10];
	void *get_dftab;        // DRV_HDR *get_dftab(const char *name)
	void *ETM_exec;
	void *cfg;
	void *basepage;
} sting_drivers_t;

// The transport layer (TPL): the client API, up to TCP_info.
typedef struct {
	const char *module;
	const char *author;
	const char *version;
	void *KRmalloc, *KRfree, *KRgetfree, *KRrealloc;
	void *get_err_text, *getvstr, *carrier_detect;
	void *TCP_open, *TCP_close, *TCP_send, *TCP_wait_state, *TCP_ack_wait;
	void *UDP_open, *UDP_close, *UDP_send;
	void *CNkick, *CNbyte_count, *CNget_char, *CNget_NDB, *CNget_block;
	void *housekeep, *resolve, *ser_disable, *ser_enable;
	void *set_flag, *clear_flag, *CNgetinfo;
	void *on_port, *off_port, *setvstr, *query_port, *CNgets;
	void *ICMP_send, *ICMP_handler, *ICMP_discard;
	void *TCP_info, *cntrl_port;
} sting_tpl_t;

// The module layer (STX), up to query_chains, for the list of ports.
typedef struct {
	const char *module;
	const char *author;
	const char *version;
	void *set_dgram_ttl, *check_dgram_ttl, *load_routing_table;
	void *set_sysvars, *query_chains;
} sting_stx_t;

// A network port: "Internal" (loopback), "WiFi", "Modem 1", ...
typedef struct sting_port {
	const char *name;
	int16_t type;
	int16_t active;
	uint32_t flags;
	uint32_t ip_addr;
	uint32_t sub_mask;
	uint16_t mtu, max_mtu;
	int32_t stat_sd_data;
	void *send;
	int32_t stat_rcv_data;
	void *receive;
	int16_t stat_dropped;
	void *driver;
	struct sting_port *next;
} sting_port_t;

#define STING_PORT_INTERNAL 0   // type of the loopback port

// What TCP_info() fills in.
typedef struct {
	uint32_t request;       // which of the following are wanted
	uint16_t state;
	uint32_t unacked;
	uint32_t srtt;
} sting_tcpib_t;

#define TCPI_STATE      0x00000001L

// TCP states.
#define TCLOSED         0
#define TLISTEN         1
#define TSYN_SENT       2
#define TSYN_RECV       3
#define TESTABLISH      4
#define TFIN_WAIT1      5
#define TFIN_WAIT2      6
#define TCLOSE_WAIT     7
#define TCLOSING        8
#define TLAST_ACK       9
#define TTIME_WAIT      10

// Error codes: every function returns one of these when it fails.
#define E_NORMAL        0
#define E_OBUFFULL      -1
#define E_NODATA        -2
#define E_EOF           -3
#define E_RRESET        -4
#define E_UA            -5
#define E_NOMEM         -6
#define E_REFUSE        -7
#define E_BADSYN        -8
#define E_BADHANDLE     -9
#define E_LISTEN        -10
#define E_NOCCB         -11
#define E_NOCONNECTION  -12
#define E_CONNECTFAIL   -13
#define E_BADCLOSE      -14
#define E_USERTIMEOUT   -15
#define E_CNTIMEOUT     -16
#define E_CANTRESOLVE   -17
#define E_BADDNAME      -18
#define E_LOSTCARRIER   -19
#define E_NOHOSTNAME    -20
#define E_DNSWORKLIMIT  -21
#define E_NONAMESERVER  -22
#define E_DNSBADFORMAT  -23
#define E_UNREACHABLE   -24
#define E_DNSNOADDR     -25
#define E_NOROUTINE     -26
#define E_LOCKED        -27
#define E_FRAGMENT      -28
#define E_TTLEXCEED     -29
#define E_PARAMETER     -30
#define E_BIGBUF        -31

#endif /* __STING_H__ */
