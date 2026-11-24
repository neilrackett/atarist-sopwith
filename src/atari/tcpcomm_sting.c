/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// TCP/IP for network games on the Atari, through the STinG stack: the
// byte stream tcpcomm.h describes, as src/tcpcomm.c provides it over
// sockets on the desktop. Both Atari backends use it.
//
// STinG does its work in the background, from the 200Hz timer, so a
// game only has to keep its connection's buffers moving: bytes sent are
// gathered and handed to STinG in one piece the next time the game
// reads (two bytes a move, rather than two one-byte segments), and
// STinG is asked for new bytes at most once a timer tick, however fast
// the game polls.
//
// This program runs in supervisor mode throughout, under either
// backend. STinG allows for that: where its functions would wait for a
// lock in user mode, they return E_LOCKED instead, and here that just
// means "not this time". STinG's resolve() is the exception - it waits
// with the AES's appl_yield whatever the mode - so names are looked up
// here instead, with one DNS query to STinG's own nameserver.

#include <string.h>

#include <mint/osbind.h>

#include "sw.h"
#include "swasynio.h"
#include "swmain.h"
#include "swtitle.h"
#include "tcpcomm.h"
#include "timer.h"

#include "sting.h"

// STinG's buffer for what we send (its output, not its window).
#define TCP_BUFFER_SIZE     2048

// How long to wait for the other player to answer a connection, and for
// a nameserver to answer a query.
#define CONNECT_TIMEOUT_MS  20000
#define DNS_TIMEOUT_MS      3000
#define DNS_TRIES           3
#define DNS_PORT            53

static sting_tpl_t *tpl;
static sting_stx_t *stx;

static int16_t tcp_handle = -1;
static bool peer_closed;            // closed, failed or reset

static uint8_t rx_buf[128];         // read from STinG, not yet given out
static int rx_len, rx_pos;
static uint8_t tx_buf[256];         // given to us, not yet sent
static int tx_len;
static int last_poll;               // Timer_GetMS() of the last look

// ---- Calling STinG ------------------------------------------------------

// Calls STinG function fn with the n bytes of arguments at args, laid out
// as Pure C pushes them (first argument first, two-byte ints), and returns
// D0: cdecl functions return pointers there too.
static long CallSting(void *fn, const void *args, int n)
{
	register long d0 __asm__("d0");
	register const uint8_t *a0 __asm__("a0") = (const uint8_t *) args + n;
	register void *a1 __asm__("a1") = fn;
	register long d1 __asm__("d1") = n / 2;

	// Push the arguments a word at a time from the last, call, and put
	// the stack back from A2, which Pure C functions preserve.
	__asm__ volatile (
		"move.l	%%sp,%%a2\n\t"
		"bra.s	2f\n"
		"1:\tmove.w	-(%%a0),-(%%sp)\n"
		"2:\tdbra	%%d1,1b\n\t"
		"jsr	(%%a1)\n\t"
		"move.l	%%a2,%%sp"
		: "=r" (d0), "+r" (a0), "+r" (a1), "+r" (d1)
		:
		: "d2", "a2", "cc", "memory");

	return d0;
}

// A compile-time check that an argument block has Pure C's size: every
// field two-byte aligned, with no padding anywhere.
#define ARGS(a, size) (void) sizeof(char[(sizeof(a) == (size)) ? 1 : -1])

static int16_t TCP_open(uint32_t host, uint16_t port, uint16_t tos,
                        uint16_t size)
{
	struct { uint32_t host; uint16_t port, tos, size; } a =
		{ host, port, tos, size };
	ARGS(a, 10);
	return (int16_t) CallSting(tpl->TCP_open, &a, sizeof(a));
}

static int16_t TCP_close(int16_t handle, int16_t mode)
{
	struct { int16_t handle, mode; int16_t *result; } a =
		{ handle, mode, NULL };
	ARGS(a, 8);
	return (int16_t) CallSting(tpl->TCP_close, &a, sizeof(a));
}

static int16_t TCP_send(int16_t handle, const void *buf, int16_t len)
{
	struct { int16_t handle; const void *buf; int16_t len; } a =
		{ handle, buf, len };
	ARGS(a, 8);
	return (int16_t) CallSting(tpl->TCP_send, &a, sizeof(a));
}

static int16_t TCP_info(int16_t handle, sting_tcpib_t *info)
{
	struct { int16_t handle; sting_tcpib_t *info; } a = { handle, info };
	ARGS(a, 6);
	return (int16_t) CallSting(tpl->TCP_info, &a, sizeof(a));
}

static int16_t UDP_open(uint32_t host, uint16_t port)
{
	struct { uint32_t host; uint16_t port; } a = { host, port };
	ARGS(a, 6);
	return (int16_t) CallSting(tpl->UDP_open, &a, sizeof(a));
}

static int16_t UDP_close(int16_t handle)
{
	struct { int16_t handle; } a = { handle };
	ARGS(a, 2);
	return (int16_t) CallSting(tpl->UDP_close, &a, sizeof(a));
}

static int16_t UDP_send(int16_t handle, const void *buf, int16_t len)
{
	struct { int16_t handle; const void *buf; int16_t len; } a =
		{ handle, buf, len };
	ARGS(a, 8);
	return (int16_t) CallSting(tpl->UDP_send, &a, sizeof(a));
}

static int16_t CNbyte_count(int16_t handle)
{
	struct { int16_t handle; } a = { handle };
	ARGS(a, 2);
	return (int16_t) CallSting(tpl->CNbyte_count, &a, sizeof(a));
}

static int16_t CNget_block(int16_t handle, void *buf, int16_t len)
{
	struct { int16_t handle; void *buf; int16_t len; } a =
		{ handle, buf, len };
	ARGS(a, 8);
	return (int16_t) CallSting(tpl->CNget_block, &a, sizeof(a));
}

// Whether p could be in memory this machine has: past the system
// variables and below the ROMs and hardware in the first 16MB (ST-RAM,
// and alt-RAM on an ST that has some), or a TT's or Falcon's fast RAM
// up to ramtop. (We are in supervisor mode.)
static bool InRam(const void *p)
{
	uintptr_t a = (uintptr_t) p;
	uintptr_t ramtop = *(volatile uint32_t *) 0x5a4L;
	uint32_t ramvalid = *(volatile uint32_t *) 0x5a8L;

	return (a >= 0x800 && a < 0x00e00000L)
	    || (ramvalid == 0x1357bd13L && a >= 0x01000000L && a < ramtop);
}

static const char *getvstr(const char *name)
{
	struct { const char *name; } a = { name };
	const char *value;

	ARGS(a, 4);
	value = (const char *) CallSting(tpl->getvstr, &a, sizeof(a));
	return InRam(value) ? value : NULL;
}

static void query_chains(sting_port_t **ports)
{
	struct { sting_port_t **ports; void **drivers, **layers; } a =
		{ ports, NULL, NULL };
	ARGS(a, 12);
	CallSting(stx->query_chains, &a, sizeof(a));
}

// ---- Finding STinG ------------------------------------------------------

static long ReadStingCookie(void)
{
	long *jar = *(long **) 0x5a0L;

	// TOS before 1.6 has no cookie jar unless something made one.
	for (; jar != NULL && jar[0] != 0; jar += 2) {
		if (jar[0] == STING_COOKIE) {
			return jar[1];
		}
	}
	return 0;
}

// One of the driver's tables, by name, checked by the name it carries
// (looked at carefully: a stray pointer would bring a bus error).
static void *GetTable(sting_drivers_t *drivers, const char *name)
{
	struct { const char *name; } a = { name };
	const char **table;

	ARGS(a, 4);
	table = (const char **) CallSting(drivers->get_dftab, &a, sizeof(a));
	if (InRam(table) && ((uintptr_t) table & 1) == 0
	 && InRam(table[0]) && strcmp(table[0], name) == 0) {
		return table;
	}
	return NULL;
}

// However the program ends, STinG must have the connection back: left
// open, it would hold the port until the ST is reset. atexit covers an
// orderly exit; GEMDOS's terminate vector covers the rest (abort(), a
// failed assert, a crash), which end the program without atexit. It
// runs as the program goes, so it only asks STinG to close, as STDL's
// own does only what cannot wait (and both of them chain on).
static void (*old_term)(void);

static void TermHandler(void)
{
	(void) Setexc(0x102, (void (*)()) old_term);
	if (tcp_handle >= 0) {
		TCP_close(tcp_handle, 0);
		tcp_handle = -1;
	}
	if (old_term != NULL) {
		old_term();
	}
}

static void RequireSting(void)
{
	sting_drivers_t *drivers;
	sting_tpl_t *found = NULL;

	if (tpl != NULL) {
		return;
	}

	drivers = (sting_drivers_t *) Supexec(ReadStingCookie);
	if (InRam(drivers) && ((uintptr_t) drivers & 1) == 0
	 && memcmp(drivers->magic, STING_MAGIC, sizeof(STING_MAGIC)) == 0) {
		found = GetTable(drivers, STING_TRANSPORT);
	}
	if (found == NULL) {
		asynerror("  Network games need the STinG\n"
		          "  TCP/IP stack, which isn't loaded");
	}

	// TCP_info, which says how a connection is going, came with STinG
	// 1.20; STiK and older STinGs have a shorter table.
	if (!InRam(found->version) || strcmp(found->version, "01.20") < 0
	 || !InRam(found->TCP_info)) {
		asynerror("  Network games need STinG 1.20\n"
		          "  or later (this is %.5s)",
		          InRam(found->version) ? found->version : "?");
	}
	tpl = found;
	stx = GetTable(drivers, STING_MODULE);

	atexit(commterm);
	old_term = (void (*)(void)) Setexc(0x102, (void (*)()) TermHandler);
}

// ---- Addresses ----------------------------------------------------------

static void FormatAddress(char *buf, uint32_t ip)
{
	sprintf(buf, "%u.%u.%u.%u", (unsigned) (ip >> 24) & 0xff,
	        (unsigned) (ip >> 16) & 0xff, (unsigned) (ip >> 8) & 0xff,
	        (unsigned) ip & 0xff);
}

// A dotted IPv4 address, read from *s up to the first character that is
// not part of one; *s is left there. Returns 0 if there is none.
static uint32_t ReadAddress(const char **s)
{
	const char *p = *s;
	uint32_t ip = 0;
	int i;

	for (i = 0; i < 4; ++i) {
		unsigned int n = 0, digits = 0;

		if (i > 0 && *p++ != '.') {
			return 0;
		}
		while (*p >= '0' && *p <= '9' && digits < 4) {
			n = n * 10 + (unsigned) (*p++ - '0');
			++digits;
		}
		if (digits == 0 || n > 255) {
			return 0;
		}
		ip = (ip << 8) | n;
	}
	*s = p;
	return ip;
}

static uint32_t ParseAddress(const char *s)
{
	uint32_t ip = ReadAddress(&s);

	return *s == '\0' ? ip : 0;
}

const char *commlocaladdr(void)
{
	static char buf[16];
	sting_port_t *port = NULL;

	RequireSting();
	if (stx == NULL) {
		return NULL;
	}
	// The first working network port; "Internal" is the loopback.
	query_chains(&port);
	for (; port != NULL; port = port->next) {
		if (port->active && port->type != STING_PORT_INTERNAL
		 && port->ip_addr != 0) {
			FormatAddress(buf, port->ip_addr);
			return buf;
		}
	}
	return NULL;
}

// ---- Looking up a name --------------------------------------------------

// Writes a DNS query for name's IPv4 address into buf; returns its size.
static int DnsQuery(uint8_t *buf, const char *name, uint16_t id)
{
	uint8_t *p = buf;
	const char *label = name;

	memset(buf, 0, 12);
	buf[0] = id >> 8;
	buf[1] = id & 0xff;
	buf[2] = 0x01;                  // recursion desired
	buf[5] = 1;                     // one question
	p = buf + 12;

	while (*label != '\0') {
		const char *end = strchr(label, '.');
		int len = end != NULL ? end - label : (int) strlen(label);

		if (len == 0 || len > 63) {
			return 0;
		}
		*p++ = (uint8_t) len;
		memcpy(p, label, len);
		p += len;
		label += len;
		if (*label == '.') {
			++label;
		}
	}
	*p++ = 0;
	*p++ = 0; *p++ = 1;             // type A
	*p++ = 0; *p++ = 1;             // class IN
	return p - buf;
}

// Steps over a (possibly compressed) name in a DNS message.
static const uint8_t *SkipName(const uint8_t *p, const uint8_t *end)
{
	while (p < end) {
		if (*p == 0) {
			return p + 1;
		} else if ((*p & 0xc0) == 0xc0) {
			return p + 2;
		}
		p += *p + 1;
	}
	return end;
}

// The first IPv4 address in a DNS reply to query id, or 0.
static uint32_t DnsAnswer(const uint8_t *msg, int len, uint16_t id)
{
	const uint8_t *p, *end = msg + len;
	int questions, answers;

	if (len < 12 || msg[0] != (id >> 8) || msg[1] != (id & 0xff)
	 || (msg[2] & 0x80) == 0 || (msg[3] & 0x0f) != 0) {
		return 0;
	}
	questions = (msg[4] << 8) | msg[5];
	answers = (msg[6] << 8) | msg[7];

	p = msg + 12;
	for (; questions > 0 && p < end; --questions) {
		p = SkipName(p, end) + 4;
	}
	for (; answers > 0 && p + 10 <= end; --answers) {
		int type, class, rdlen;

		p = SkipName(p, end);
		if (p + 10 > end) {
			break;
		}
		type = (p[0] << 8) | p[1];
		class = (p[2] << 8) | p[3];
		rdlen = (p[8] << 8) | p[9];
		p += 10;
		if (type == 1 && class == 1 && rdlen == 4 && p + 4 <= end) {
			return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16)
			     | ((uint32_t) p[2] << 8) | p[3];
		}
		p += rdlen;
	}
	return 0;
}

// Asks one nameserver; 0 if it does not know or does not answer.
static uint32_t AskNameserver(uint32_t server, const char *name)
{
	static uint8_t msg[512];
	uint16_t id = (uint16_t) (Timer_GetMS() ^ 0x5357);
	uint32_t ip = 0;
	int16_t handle;
	int len, tries;

	len = DnsQuery(msg, name, id);
	if (len == 0 || (handle = UDP_open(server, DNS_PORT)) < 0) {
		return 0;
	}

	for (tries = 0; tries < DNS_TRIES && ip == 0; ++tries) {
		int start = Timer_GetMS();
		int16_t n = 0;

		if (UDP_send(handle, msg, len) < 0) {
			break;
		}
		while (Timer_GetMS() - start < DNS_TIMEOUT_MS) {
			if (ctlbreak()) {
				UDP_close(handle);
				asynerror(NULL);
			}
			n = CNbyte_count(handle);
			if (n > 0) {
				break;
			}
		}
		if (n <= 0) {
			continue;
		}
		if (n > (int16_t) sizeof(msg)) {
			n = sizeof(msg);
		}
		n = CNget_block(handle, msg, n);
		if (n > 0) {
			ip = DnsAnswer(msg, n, id);
		}
		// Whatever came back, the query is rewritten for any retry.
		len = DnsQuery(msg, name, id);
	}

	UDP_close(handle);
	return ip;
}

// The IPv4 address of a host name, from the nameservers STinG is set
// up with (its NAMESERVER setting: addresses separated by commas or
// spaces), or 0.
static uint32_t LookUp(const char *name)
{
	const char *servers = getvstr("NAMESERVER");

	while (servers != NULL && *servers != '\0') {
		uint32_t server = ReadAddress(&servers);

		if (server != 0) {
			uint32_t ip = AskNameserver(server, name);
			if (ip != 0) {
				return ip;
			}
		} else {
			++servers;
		}
	}
	return 0;
}

// ---- Connections --------------------------------------------------------

static const char *ErrorText(int16_t error)
{
	static char buf[24];

	switch (error) {
	case E_REFUSE:
	case E_RRESET:
		return "the connection was refused";
	case E_UNREACHABLE:
		return "no route to it";
	case E_CNTIMEOUT:
	case E_CONNECTFAIL:
	case E_USERTIMEOUT:
		return "there was no answer";
	case E_LOSTCARRIER:
		return "the network is down";
	case E_NOMEM:
	case E_NOCCB:
		return "STinG is out of memory";
	default:
		sprintf(buf, "STinG error %d", error);
		return buf;
	}
}

// (Not the timer: commterm runs at exit, and under STDL that can be
// after the return to user mode, where the timer cannot be read.)
static void ResetBuffers(void)
{
	rx_len = rx_pos = tx_len = 0;
	peer_closed = false;
	last_poll = -1;
}

// A port number, checked: to TCP_open, 0 and 65535 mean the address is
// really a pointer to a block of connection details.
static uint16_t CheckPort(long port)
{
	if (port <= 0 || port >= 65535) {
		asynerror("  %ld isn't a port number", port);
	}
	return (uint16_t) port;
}

// Waits until the connection is established: true, or false after
// timeout_ms (0 for no limit) or on failure, *error saying which.
static bool WaitConnected(int timeout_ms, int16_t *error)
{
	int start = Timer_GetMS();
	sting_tcpib_t info;

	for (;;) {
		int16_t rc;

		if (ctlbreak()) {
			commterm();
			asynerror(NULL);
		}

		info.request = TCPI_STATE;
		rc = TCP_info(tcp_handle, &info);
		if (rc < 0 && rc != E_LOCKED) {
			*error = rc;
			return false;
		} else if (rc >= 0 && info.state >= TESTABLISH) {
			// Established, or even already closing (a peer can
			// connect and go between two looks): commin will say.
			return true;
		} else if (rc >= 0 && info.state == TCLOSED) {
			*error = E_REFUSE;
			return false;
		}

		if (timeout_ms != 0 && Timer_GetMS() - start > timeout_ms) {
			*error = E_CNTIMEOUT;
			return false;
		}
	}
}

// connect to a host ("name" or "name:port")
void commconnect(char *host)
{
	char name[64], *colon;
	uint16_t port;
	uint32_t ip;
	int16_t error;

	RequireSting();
	commterm();

	strncpy(name, host, sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	colon = strchr(name, ':');
	if (colon != NULL) {
		*colon = '\0';
		port = CheckPort(atol(colon + 1));
	} else {
		port = CheckPort(asynport);
	}

	ip = ParseAddress(name);
	if (ip == 0) {
		ip = LookUp(name);
	}
	if (ip == 0) {
		asynerror("  Can't find the address of\n  %.36s", name);
	}

	tcp_handle = TCP_open(ip, port, 0, TCP_BUFFER_SIZE);
	if (tcp_handle < 0) {
		error = tcp_handle;
		tcp_handle = -1;
		asynerror("  Can't connect to %.19s:\n  %s",
		          name, ErrorText(error));
	}
	ResetBuffers();

	if (!WaitConnected(CONNECT_TIMEOUT_MS, &error)) {
		commterm();
		asynerror("  Can't connect to %.19s:\n  %s",
		          name, ErrorText(error));
	}
}

// open a socket and listen until a connection is established
void commlisten(void)
{
	uint16_t port;
	int16_t error;

	RequireSting();
	commterm();
	port = CheckPort(asynport);

	// No remote host: a passive open, waiting on our own port.
	tcp_handle = TCP_open(0, port, 0, TCP_BUFFER_SIZE);
	if (tcp_handle < 0) {
		error = tcp_handle;
		tcp_handle = -1;
		asynerror("  Can't listen on port %d:\n  %s",
		          asynport, ErrorText(error));
	}
	ResetBuffers();

	if (!WaitConnected(0, &error)) {
		commterm();
		asynerror("  The connection failed:\n  %s", ErrorText(error));
	}
}

// Whether an error from STinG means the connection is gone. Others are
// news of the moment (an ICMP message, a datagram STinG could not route
// this time, no memory just now) that leave it as it was: if it has
// really died, the game's own wait for the other player gives up.
static bool IsFatal(int16_t error)
{
	switch (error) {
	case E_EOF:
	case E_RRESET:
	case E_REFUSE:
	case E_CNTIMEOUT:
	case E_CONNECTFAIL:
	case E_NOCONNECTION:
	case E_BADHANDLE:
	case E_BADCLOSE:
		return true;
	default:
		return false;
	}
}

// Hands what has been written to STinG, if it will take it now.
static void Flush(void)
{
	int16_t rc;

	if (tx_len == 0 || tcp_handle < 0 || peer_closed) {
		return;
	}
	rc = TCP_send(tcp_handle, tx_buf, (int16_t) tx_len);
	if (rc == E_NORMAL) {
		tx_len = 0;
	} else if (IsFatal(rc)) {
		peer_closed = true;
	}
}

// read a byte from the connection, or -1 if there is none yet
int commin(void)
{
	int now;
	int16_t n;

	// What we have written goes first: a move should not wait while the
	// other player's moves are read, one a frame.
	Flush();

	if (rx_pos < rx_len) {
		return rx_buf[rx_pos++];
	}
	if (tcp_handle < 0) {
		return -1;
	}

	now = Timer_GetMS();
	if (peer_closed || now == last_poll) {
		return -1;
	}
	last_poll = now;

	n = CNbyte_count(tcp_handle);
	if (n > 0) {
		if (n > (int16_t) sizeof(rx_buf)) {
			n = sizeof(rx_buf);
		}
		n = CNget_block(tcp_handle, rx_buf, n);
		if (n > 0) {
			rx_len = n;
			rx_pos = 1;
			return rx_buf[0];
		}
	}

	// Nothing yet, or never again: E_EOF once the other end has closed
	// and everything it sent has been read, or the connection's end.
	if (n < 0 && IsFatal(n)) {
		peer_closed = true;
	}
	return -1;
}

// send a byte
void commout(unsigned char c)
{
	if (tcp_handle < 0) {
		return;
	}
	if (tx_len == (int) sizeof(tx_buf)) {
		Flush();
		if (tx_len == (int) sizeof(tx_buf)) {
			// Nothing has left for a long time: the other end has
			// stopped reading.
			peer_closed = true;
			return;
		}
	}
	tx_buf[tx_len++] = c;
}

bool commclosed(void)
{
	return peer_closed && rx_pos >= rx_len;
}

// disconnect
void commterm(void)
{
	int16_t n;
	int i;

	if (tcp_handle < 0) {
		return;
	}

	Flush();

	// STinG resets a connection closed with data unread, and the other
	// end may lose what it had yet to read from us: read it first, so
	// the close is an orderly one.
	for (i = 0; i < 64 && (n = CNbyte_count(tcp_handle)) > 0; ++i) {
		if (n > (int16_t) sizeof(rx_buf)) {
			n = sizeof(rx_buf);
		}
		if (CNget_block(tcp_handle, rx_buf, n) <= 0) {
			break;
		}
	}

	// A timeout of 0: STinG finishes closing it in the background.
	TCP_close(tcp_handle, 0);
	tcp_handle = -1;
	ResetBuffers();
}

// From choosing a network game to its end, connected or not yet: the
// keys that restart a level or a game are not the player's alone then.
bool isNetworkGame(void)
{
	return tcp_handle >= 0 || playmode == PLAYMODE_ASYNCH;
}
