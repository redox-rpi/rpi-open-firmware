/*
 * net.c: Ethernet, ARP, IPv4, UDP, a DHCP client and a TFTP client for
 * network boot (rof #119 N4, #120 N5).
 *
 * Polled, one frame at a time, over the LAN7800 driver (lan78xx.c). Only
 * what a boot loader needs: no IP fragments (TFTP blocks stay below the
 * MTU), no IP options, UDP checksums not checked when zero.
 *
 * TFTP: RFC 1350 with the options blksize (RFC 2348), tsize (RFC 2349)
 * and windowsize (RFC 7440). A server that ignores the options still
 * works, with 512-byte blocks in lock-step. After U-Boot's net/tftp.c
 * (GPL-2.0+).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"
#include "lan78xx.h"
#include "net.h"
#include "usb.h"
#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n); /* libc.c */
void *memset(void *dst, int c, size_t n);

#define ETH_ARP 0x0806u
#define ETH_IP 0x0800u
#define PORT_DHCP_S 67u
#define PORT_DHCP_C 68u

static uint8_t g_mac[6];
static uint32_t g_ip, g_mask, g_router, g_dhcp_server, g_next_server;
static bool g_up;
static uint16_t g_ip_id;
static uint32_t g_arp_ip; /* the last ARP reply */
static uint8_t g_arp_mac[6];
static uint8_t g_frame[1536]; /* received */
static uint8_t g_tx[1536];

/* the UDP packet handed out by net_poll */
static const uint8_t *g_udp_data;
static uint32_t g_udp_len, g_udp_src_ip;
static uint16_t g_udp_src_port, g_udp_dst_port;

static uint16_t be16(const uint8_t *p) {
	return (uint16_t)(p[0] << 8 | p[1]);
}
static uint32_t be32(const uint8_t *p) {
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static void put16(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)(v >> 8), p[1] = (uint8_t)v;
}
static void put32(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v;
}

static uint16_t ip_sum(const uint8_t *p, uint32_t n) {
	uint32_t s = 0;
	for (uint32_t i = 0; i + 1 < n; i += 2)
		s += (uint32_t)(p[i] << 8 | p[i + 1]);
	if (n & 1)
		s += (uint32_t)p[n - 1] << 8;
	while (s >> 16)
		s = (s & 0xffffu) + (s >> 16);
	return (uint16_t)~s;
}

void net_print_ip(uint32_t ip) {
	printf("%u.%u.%u.%u", ip >> 24, (ip >> 16) & 0xffu, (ip >> 8) & 0xffu, ip & 0xffu);
}

static int eth_send(const uint8_t *dst, uint32_t type, uint32_t len) {
	memcpy(g_tx, dst, 6);
	memcpy(g_tx + 6, g_mac, 6);
	put16(g_tx + 12, type);
	return lan78xx_send(g_tx, 14u + len);
}

static void arp_send(uint32_t op, const uint8_t *tha, uint32_t tpa) {
	static const uint8_t bcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	uint8_t *a = g_tx + 14;
	put16(a, 1), put16(a + 2, ETH_IP), a[4] = 6, a[5] = 4, put16(a + 6, op);
	memcpy(a + 8, g_mac, 6);
	put32(a + 14, g_ip);
	memcpy(a + 18, op == 2 ? tha : (const uint8_t *)"\0\0\0\0\0\0", 6);
	put32(a + 24, tpa);
	eth_send(op == 2 ? tha : bcast, ETH_ARP, 28);
}

/*
 * One receive step: answers ARP requests for our IP, notes ARP replies,
 * and returns true with g_udp_* set when a UDP packet for us arrived.
 */
static bool net_poll(void) {
	int n = lan78xx_recv(g_frame, sizeof g_frame);
	if (n < 14)
		return false;
	const uint8_t *f = g_frame;
	uint32_t type = be16(f + 12);
	if (type == ETH_ARP && n >= 42) {
		const uint8_t *a = f + 14;
		uint32_t op = be16(a + 6), spa = be32(a + 14), tpa = be32(a + 24);
		if (op == 1 && g_ip && tpa == g_ip)
			arp_send(2, a + 8, spa);
		else if (op == 2) {
			g_arp_ip = spa;
			memcpy(g_arp_mac, a + 8, 6);
		}
		return false;
	}
	if (type != ETH_IP || n < 14 + 20 + 8)
		return false;
	const uint8_t *ip = f + 14;
	uint32_t ihl = (ip[0] & 0xfu) * 4u, tot = be16(ip + 2);
	if ((ip[0] >> 4) != 4 || ihl < 20 || tot > (uint32_t)n - 14u || tot < ihl + 8u)
		return false;
	if ((be16(ip + 6) & 0x3fffu) || ip[9] != 17) /* fragments, not UDP */
		return false;
	uint32_t dst = be32(ip + 16);
	if (g_ip && dst != g_ip && dst != 0xffffffffu)
		return false;
	const uint8_t *u = ip + ihl;
	uint32_t ulen = be16(u + 4);
	if (ulen < 8 || ulen > tot - ihl)
		return false;
	g_udp_src_ip = be32(ip + 12);
	g_udp_src_port = be16(u);
	g_udp_dst_port = be16(u + 2);
	g_udp_data = u + 8;
	g_udp_len = ulen - 8;
	return true;
}

/* send a UDP packet; the payload is already at g_tx + 42 */
static int udp_send(const uint8_t *dmac, uint32_t dip, uint32_t sport, uint32_t dport, uint32_t len) {
	uint8_t *ip = g_tx + 14, *u = ip + 20;
	ip[0] = 0x45, ip[1] = 0;
	put16(ip + 2, 28u + len);
	put16(ip + 4, g_ip_id++);
	put16(ip + 6, 0x4000); /* don't fragment */
	ip[8] = 64, ip[9] = 17;
	put16(ip + 10, 0);
	put32(ip + 12, g_ip);
	put32(ip + 16, dip);
	put16(ip + 10, ip_sum(ip, 20));
	put16(u, sport), put16(u + 2, dport), put16(u + 4, 8u + len), put16(u + 6, 0);
	return eth_send(dmac, ETH_IP, 28u + len);
}

/* the MAC address for ip: itself on our subnet, else the router; ARP, 3 tries */
static bool arp_resolve(uint32_t ip, uint8_t *mac) {
	uint32_t hop = (ip & g_mask) == (g_ip & g_mask) ? ip : g_router;
	if (!hop)
		return false;
	g_arp_ip = 0;
	for (int t = 0; t < 3; t++) {
		arp_send(1, 0, hop);
		uint32_t t0 = timer_us();
		while (timer_us() - t0 < 1000000u) {
			net_poll();
			if (g_arp_ip == hop) {
				memcpy(mac, g_arp_mac, 6);
				return true;
			}
		}
	}
	return false;
}

/* bring the LAN7800 up once */
static bool net_up(void) {
	if (g_up)
		return true;
	if (!usb_probe_root())
		return false;
	const struct usb_dev *dev = usb_find(0x0424, 0x7800);
	if (!dev) {
		printf("err net: no LAN7800 (0424:7800) found\n");
		return false;
	}
	if (!lan78xx_start(dev, 5000, 0))
		return false;
	memcpy(g_mac, lan78xx_mac(), 6);
	g_up = true;
	return true;
}

/* ---- DHCP (RFC 2131) ---- */

static uint32_t dhcp_build(uint32_t xid, uint32_t type, uint32_t req_ip, uint32_t server) {
	uint8_t *d = g_tx + 42;
	memset(d, 0, 240);
	d[0] = 1, d[1] = 1, d[2] = 6; /* BOOTREQUEST, Ethernet */
	put32(d + 4, xid);
	put16(d + 10, 0x8000); /* answer by broadcast: we have no IP yet */
	memcpy(d + 28, g_mac, 6);
	put32(d + 236, 0x63825363); /* magic cookie */
	uint8_t *o = d + 240;
	*o++ = 53, *o++ = 1, *o++ = (uint8_t)type;
	if (req_ip)
		*o++ = 50, *o++ = 4, put32(o, req_ip), o += 4;
	if (server)
		*o++ = 54, *o++ = 4, put32(o, server), o += 4;
	*o++ = 55, *o++ = 4, *o++ = 1, *o++ = 3, *o++ = 54, *o++ = 66; /* mask, router, server, TFTP */
	*o++ = 255;
	uint32_t len = (uint32_t)(o - d);
	return len < 300 ? 300 : len; /* some servers want the BOOTP minimum */
}

/* wait for a DHCP reply of type want to xid; fills the fields; false on timeout */
static bool dhcp_wait(uint32_t xid, uint32_t want, uint32_t us, uint32_t *yiaddr) {
	uint32_t t0 = timer_us();
	while (timer_us() - t0 < us) {
		if (!net_poll() || g_udp_dst_port != PORT_DHCP_C || g_udp_len < 240)
			continue;
		const uint8_t *d = g_udp_data;
		if (d[0] != 2 || be32(d + 4) != xid || be32(d + 236) != 0x63825363)
			continue;
		uint32_t type = 0, mask = 0, router = 0, server = 0;
		for (uint32_t i = 240; i + 1 < g_udp_len && d[i] != 255;) {
			if (d[i] == 0) {
				i++;
				continue;
			}
			uint32_t c = d[i], l = d[i + 1];
			const uint8_t *v = d + i + 2;
			if (i + 2 + l > g_udp_len)
				break;
			if (c == 53 && l >= 1)
				type = v[0];
			else if (c == 1 && l >= 4)
				mask = be32(v);
			else if (c == 3 && l >= 4)
				router = be32(v);
			else if (c == 54 && l >= 4)
				server = be32(v);
			i += 2 + l;
		}
		uint32_t yi = be32(d + 16);
		if (type != want || !yi) /* a proxyDHCP offer has no address for us */
			continue;
		*yiaddr = yi;
		g_mask = mask ? mask : 0xffffff00u;
		g_router = router;
		g_dhcp_server = server;
		g_next_server = be32(d + 20);
		return true;
	}
	return false;
}

bool net_dhcp(void) {
	static const uint8_t bcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	if (!net_up())
		return false;
	uint32_t t0 = timer_us();
	uint32_t xid = timer_us() ^ board_serial();
	g_ip = 0;
	for (int t = 0; t < 4; t++) {
		uint32_t yi = 0;
		udp_send(bcast, 0xffffffffu, PORT_DHCP_C, PORT_DHCP_S, dhcp_build(xid, 1, 0, 0));
		if (!dhcp_wait(xid, 2, 2000000u, &yi))
			continue;
		uint32_t server = g_dhcp_server;
		udp_send(bcast, 0xffffffffu, PORT_DHCP_C, PORT_DHCP_S, dhcp_build(xid, 3, yi, server));
		if (!dhcp_wait(xid, 5, 2000000u, &yi))
			continue;
		g_ip = yi;
		printf("net: DHCP in %u ms: IP ", (timer_us() - t0) / 1000u);
		net_print_ip(g_ip);
		printf(", mask ");
		net_print_ip(g_mask);
		printf(", router ");
		net_print_ip(g_router);
		printf(", DHCP server ");
		net_print_ip(g_dhcp_server);
		printf(", next server ");
		net_print_ip(g_next_server);
		printf("\n");
		return true;
	}
	printf("err net: no DHCP answer\n");
	return false;
}

/* ---- TFTP ---- */

#define TFTP_RRQ 1u
#define TFTP_DATA 3u
#define TFTP_ACK 4u
#define TFTP_ERROR 5u
#define TFTP_OACK 6u

static uint32_t opt_put(uint8_t *p, const char *s) {
	uint32_t n = 0;
	while (s[n])
		p[n] = (uint8_t)s[n], n++;
	p[n] = 0;
	return n + 1;
}

static uint32_t num_put(uint8_t *p, uint32_t v) {
	char b[12];
	int i = 11;
	b[i] = 0;
	do
		b[--i] = (char)('0' + v % 10u);
	while ((v /= 10u) && i > 0);
	return opt_put(p, b + i);
}

static bool str_eq(const uint8_t *a, const char *b) {
	for (; *b; a++, b++)
		if ((*a | 0x20) != (*b | 0x20))
			return false;
	return *a == 0;
}

static uint32_t str_num(const uint8_t *p) {
	uint32_t v = 0;
	while (*p >= '0' && *p <= '9')
		v = v * 10u + (uint32_t)(*p++ - '0');
	return v;
}

int net_tftp(uint32_t server, uint32_t port, const char *file, uint8_t *dst, uint32_t max, uint32_t blksize,
             uint32_t window) {
	if (!g_ip) {
		printf("err tftp: no IP, run dhcp first\n");
		return -1;
	}
	uint8_t smac[6];
	if (!arp_resolve(server, smac)) {
		printf("err tftp: no ARP answer from ");
		net_print_ip(server);
		printf("\n");
		return -1;
	}
	uint32_t lport = 49152u + (timer_us() & 0x3fffu);
	uint8_t *r = g_tx + 42;
	uint32_t rl = 0;
	put16(r, TFTP_RRQ);
	rl = 2;
	rl += opt_put(r + rl, file);
	rl += opt_put(r + rl, "octet");
	if (blksize != 512 || window > 1) {
		rl += opt_put(r + rl, "blksize");
		rl += num_put(r + rl, blksize);
		rl += opt_put(r + rl, "tsize");
		rl += num_put(r + rl, 0);
		if (window > 1) {
			rl += opt_put(r + rl, "windowsize");
			rl += num_put(r + rl, window);
		}
	}
	uint32_t t0 = timer_us();
	udp_send(smac, server, lport, port, rl);

	uint32_t blk = 512, win = 1, tsize = 0;
	uint32_t sport = 0;      /* the server's port, from its first answer */
	uint32_t got = 0;        /* blocks in order so far */
	uint32_t in_win = 0;     /* blocks since our last ACK */
	uint32_t bytes = 0, resend = 0, dup = 0, tries = 0;
	bool started = false, done = false;
	uint32_t last_rx = timer_us();
	while (!done) {
		if (timer_us() - last_rx > 1000000u) {
			if (++tries > 6) {
				printf("err tftp: timeout after %u bytes\n", bytes);
				return -1;
			}
			last_rx = timer_us();
			if (!started) {
				/* the request is still at g_tx + 42: only ARP replies were sent */
				udp_send(smac, server, lport, port, rl);
			} else {
				uint8_t *a = g_tx + 42;
				put16(a, TFTP_ACK), put16(a + 2, got & 0xffffu);
				udp_send(smac, server, lport, sport, 4);
				resend++;
			}
			continue;
		}
		if (!net_poll() || g_udp_dst_port != lport || g_udp_src_ip != server || g_udp_len < 4)
			continue;
		if (sport && g_udp_src_port != sport)
			continue;
		const uint8_t *p = g_udp_data;
		uint32_t op = be16(p);
		if (op == TFTP_ERROR) {
			printf("err tftp: server error %u: %s\n", be16(p + 2), g_udp_len > 4 ? (const char *)p + 4 : "");
			return -1;
		}
		if (!sport) {
			sport = g_udp_src_port;
			started = true;
		}
		last_rx = timer_us();
		tries = 0;
		if (op == TFTP_OACK && got == 0) {
			for (uint32_t i = 2; i < g_udp_len;) {
				const uint8_t *k = p + i;
				while (i < g_udp_len && p[i])
					i++;
				const uint8_t *v = p + ++i;
				while (i < g_udp_len && p[i])
					i++;
				i++;
				if (str_eq(k, "blksize"))
					blk = str_num(v);
				else if (str_eq(k, "windowsize"))
					win = str_num(v);
				else if (str_eq(k, "tsize"))
					tsize = str_num(v);
			}
			if (blk < 8 || blk > 1468)
				blk = 512;
			if (!win)
				win = 1;
			uint8_t *a = g_tx + 42;
			put16(a, TFTP_ACK), put16(a + 2, 0);
			udp_send(smac, server, lport, sport, 4);
			continue;
		}
		if (op != TFTP_DATA)
			continue;
		uint32_t b = be16(p + 2), len = g_udp_len - 4;
		if (b != ((got + 1u) & 0xffffu)) {
			/* out of order or a repeat: ack the last good block, the server goes on from there */
			dup++;
			if (b != (got & 0xffffu)) {
				uint8_t *a = g_tx + 42;
				put16(a, TFTP_ACK), put16(a + 2, got & 0xffffu);
				udp_send(smac, server, lport, sport, 4);
				in_win = 0;
			}
			continue;
		}
		if (bytes + len > max) {
			printf("err tftp: file larger than %u bytes\n", max);
			return -1;
		}
		memcpy(dst + bytes, p + 4, len);
		bytes += len;
		got++;
		in_win++;
		done = len < blk;
		if (done || in_win >= win) {
			uint8_t *a = g_tx + 42;
			put16(a, TFTP_ACK), put16(a + 2, got & 0xffffu);
			udp_send(smac, server, lport, sport, 4);
			in_win = 0;
		}
	}
	uint32_t us = timer_us() - t0;
	uint32_t kbps = us ? (uint32_t)(((uint64_t)bytes * 1000u) / us) : 0; /* KB/s, 1 KB = 1000 B */
	printf("tftp: %s: %u bytes (tsize %u) in %u ms, %u.%02u MB/s, blksize %u, window %u, "
	       "%u timeouts, %u out of order, crc32 %08X\n",
	       file, bytes, tsize, us / 1000u, kbps / 1000u, (kbps % 1000u) / 10u, blk, win, resend, dup,
	       crc32(dst, bytes));
	return (int)bytes;
}

uint32_t net_parse_ip(const char *s, bool *ok) {
	uint32_t ip = 0, part = 0, dots = 0, digits = 0;
	for (; *s; s++) {
		if (*s >= '0' && *s <= '9') {
			part = part * 10u + (uint32_t)(*s - '0');
			if (part > 255 || ++digits > 3)
				break;
		} else if (*s == '.' && digits && dots < 3) {
			ip = ip << 8 | part, part = 0, digits = 0, dots++;
		} else
			break;
	}
	*ok = !*s && dots == 3 && digits;
	return ip << 8 | part;
}
