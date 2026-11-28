/*
 * lan78xx.c: the LAN7800 Ethernet chip of the Pi 3B+ (inside the LAN7515),
 * for network boot (rof #122 N7).
 *
 * Register access over vendor control requests, chip reset, MAC address,
 * PHY autonegotiation and link, and frames over the bulk endpoints. One
 * frame per bulk transfer for now (BURST_CAP 0), as U-Boot does.
 *
 * Ported in trimmed form from U-Boot drivers/usb/eth/lan78xx.c and lan7x.c
 * (GPL-2.0+). The 3B+ has no EEPROM for the LAN7800; its MAC address is
 * b8:27:eb plus the low 24 bits of the board serial number, as start.elf
 * puts it into the devicetree (mbox.c).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"
#include "lan78xx.h"
#include "usb.h"
#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n); /* libc.c */

#define ID_REV 0x000
#define INT_STS 0x00c
#define HW_CFG 0x010
#define HW_CFG_LRST (1u << 1)
#define PMT_CTL 0x014
#define PMT_CTL_READY (1u << 7)
#define PMT_CTL_PHY_RST (1u << 4)
#define USB_CFG0 0x080
#define USB_CFG0_BIR (1u << 6)
#define BURST_CAP 0x090
#define BULK_IN_DLY 0x094
#define RFE_CTL 0x0b0
#define RFE_CTL_BCAST_EN (1u << 10)
#define RFE_CTL_DA_PERFECT (1u << 1)
#define FCT_RX_CTL 0x0c0
#define FCT_TX_CTL 0x0c4
#define FCT_RX_FIFO_END 0x0c8
#define FCT_TX_FIFO_END 0x0cc
#define FCT_EN (1u << 31)
#define MAC_CR 0x100
#define MAC_CR_AUTO (1u << 13 | 1u << 12 | 1u << 11) /* ADP, auto duplex, auto speed */
#define MAC_RX 0x104
#define MAC_RX_FCS_STRIP (1u << 4)
#define MAC_RX_RXEN (1u << 0)
#define MAC_TX 0x108
#define MAC_TX_TXEN (1u << 0)
#define FLOW 0x10c
#define RX_ADDRH 0x118
#define RX_ADDRL 0x11c
#define MII_ACC 0x120
#define MII_ACC_WRITE (1u << 1)
#define MII_ACC_BUSY (1u << 0)
#define MII_DATA 0x124
#define MAF_HI0 0x400
#define MAF_LO0 0x404
#define MAF_HI_VALID (1u << 31)

#define PHY_ID 1u
#define MII_BMCR 0
#define MII_BMSR 1
#define MII_ADVERTISE 4
#define MII_LPA 5
#define MII_CTRL1000 9
#define MII_STAT1000 10
#define BMCR_ANENABLE 0x1000u
#define BMCR_ANRESTART 0x0200u
#define BMSR_LSTATUS 0x0004u
#define BMSR_ANEGCOMPLETE 0x0020u

#define TX_CMD_A_FCS (1u << 22)
#define RX_CMD_A_RXE (1u << 18)
#define RX_HDR 10u /* rx_cmd_a, rx_cmd_b, rx_cmd_c */
#define MAX_FIFO (12u * 1024u)
#define FRAME_MAX 1518u

static struct usb_ep g_ep0, g_in, g_out;
static uint32_t g_tog_in, g_tog_out;
static uint8_t g_mac[6];

static int reg_write(uint32_t reg, uint32_t val) {
	uint8_t b[4] = {(uint8_t)val, (uint8_t)(val >> 8), (uint8_t)(val >> 16), (uint8_t)(val >> 24)};
	struct usb_setup s = {0x40, 0xa0, 0, (uint16_t)reg, 4}; /* vendor, write register */
	int r = usb_control(&g_ep0, &s, b);
	return r == 4 ? 0 : r < 0 ? r : -7;
}

static int reg_read(uint32_t reg, uint32_t *val) {
	uint8_t b[4];
	struct usb_setup s = {0xc0, 0xa1, 0, (uint16_t)reg, 4}; /* vendor, read register */
	int r = usb_control(&g_ep0, &s, b);
	if (r != 4)
		return r < 0 ? r : -7;
	*val = (uint32_t)(b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24);
	return 0;
}

/* wait until (reg & mask) is all set (set) or all clear (!set) */
static int reg_wait(uint32_t reg, uint32_t mask, bool set, uint32_t ms) {
	uint32_t t0 = timer_us(), v = 0;
	for (;;) {
		int r = reg_read(reg, &v);
		if (r < 0)
			return r;
		if (((set ? v : ~v) & mask) == mask)
			return 0;
		if (timer_us() - t0 > ms * 1000u)
			return -8;
		udelay(1000);
	}
}

static int mdio_read(uint32_t idx) {
	uint32_t v;
	if (reg_wait(MII_ACC, MII_ACC_BUSY, false, 100) < 0)
		return -8;
	reg_write(MII_ACC, PHY_ID << 11 | idx << 6 | MII_ACC_BUSY);
	if (reg_wait(MII_ACC, MII_ACC_BUSY, false, 100) < 0 || reg_read(MII_DATA, &v) < 0)
		return -8;
	return (int)(v & 0xffffu);
}

static int mdio_write(uint32_t idx, uint32_t val) {
	if (reg_wait(MII_ACC, MII_ACC_BUSY, false, 100) < 0)
		return -8;
	reg_write(MII_DATA, val);
	reg_write(MII_ACC, PHY_ID << 11 | idx << 6 | MII_ACC_WRITE | MII_ACC_BUSY);
	return reg_wait(MII_ACC, MII_ACC_BUSY, false, 100);
}

const uint8_t *lan78xx_mac(void) {
	return g_mac;
}

/*
 * Bring up the LAN7800 found by usb_probe_root(): reset, MAC address, PHY
 * autonegotiation, link, then the TX and RX paths. Prints one line per step.
 */
bool lan78xx_start(const struct usb_dev *dev, uint32_t link_ms) {
	g_ep0 = dev->ep0;
	if (!usb_bulk_endpoints(dev, &g_in, &g_out)) {
		printf("err eth: no bulk endpoints\n");
		return false;
	}
	g_tog_in = g_tog_out = 0; /* DATA0 after SET_CONFIGURATION */
	uint32_t id = 0, v = 0;
	int r = reg_write(HW_CFG, HW_CFG_LRST);
	if (r == 0)
		r = reg_wait(HW_CFG, HW_CFG_LRST, false, 1000);
	if (r == 0)
		r = reg_read(PMT_CTL, &v);
	if (r == 0)
		r = reg_write(PMT_CTL, v | PMT_CTL_PHY_RST);
	if (r == 0)
		r = reg_wait(PMT_CTL, PMT_CTL_PHY_RST, false, 1000);
	if (r == 0)
		r = reg_wait(PMT_CTL, PMT_CTL_READY, true, 1000);
	if (r == 0)
		r = reg_read(ID_REV, &id);
	if (r < 0) {
		printf("err eth: chip reset failed: %d\n", r);
		return false;
	}
	reg_read(USB_CFG0, &v);
	reg_write(USB_CFG0, v & ~USB_CFG0_BIR); /* NAK when no frame is there */

	uint32_t serial = board_serial();
	const uint8_t mac[6] = {0xb8, 0x27, 0xeb, (uint8_t)(serial >> 16), (uint8_t)(serial >> 8),
	                        (uint8_t)serial};
	memcpy(g_mac, mac, 6);
	uint32_t lo = (uint32_t)(mac[0] | mac[1] << 8 | mac[2] << 16 | mac[3] << 24);
	uint32_t hi = (uint32_t)(mac[4] | mac[5] << 8);
	reg_write(RX_ADDRL, lo);
	reg_write(RX_ADDRH, hi);
	reg_write(MAF_LO0, lo);
	reg_write(MAF_HI0, hi | MAF_HI_VALID);
	printf("eth: LAN%04X rev %u, endpoints in %u out %u (max packet %u), MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
	       id >> 16, id & 0xffffu, g_in.ep, g_out.ep, g_in.mps, mac[0], mac[1], mac[2], mac[3],
	       mac[4], mac[5]);

	reg_write(BURST_CAP, 0);
	reg_write(BULK_IN_DLY, 0x800);
	reg_write(INT_STS, 0xffffffffu);
	reg_write(FCT_RX_FIFO_END, (MAX_FIFO - 512u) / 512u);
	reg_write(FCT_TX_FIFO_END, (MAX_FIFO - 512u) / 512u);
	reg_write(FLOW, 0);
	reg_write(RFE_CTL, RFE_CTL_BCAST_EN | RFE_CTL_DA_PERFECT);

	/* the PHY comes up advertising every mode; restart autonegotiation */
	mdio_write(MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);
	uint32_t t0 = timer_us();
	int bmsr = 0;
	do {
		udelay(10000);
		bmsr = mdio_read(MII_BMSR);
	} while (bmsr >= 0 && (bmsr & (BMSR_LSTATUS | BMSR_ANEGCOMPLETE)) !=
	                          (BMSR_LSTATUS | BMSR_ANEGCOMPLETE) &&
	         timer_us() - t0 < link_ms * 1000u);
	uint32_t ms = (timer_us() - t0) / 1000u;
	if (bmsr < 0 || !(bmsr & BMSR_LSTATUS)) {
		printf("err eth: no link after %u ms (BMSR %04X)\n", ms, bmsr & 0xffff);
		return false;
	}
	int adv = mdio_read(MII_ADVERTISE), lpa = mdio_read(MII_LPA);
	int c1000 = mdio_read(MII_CTRL1000), s1000 = mdio_read(MII_STAT1000);
	uint32_t common = (uint32_t)(adv & lpa);
	const char *mode = (c1000 & 0x200) && (s1000 & 0x800) ? "1000 full"
	                   : common & 0x100                    ? "100 full"
	                   : common & 0x080                    ? "100 half"
	                   : common & 0x040                    ? "10 full"
	                                                       : "10 half";
	printf("eth: link up after %u ms, %s duplex (LPA %04X, 1000 %04X)\n", ms, mode, lpa & 0xffff,
	       s1000 & 0xffff);

	reg_read(MAC_CR, &v);
	reg_write(MAC_CR, v | MAC_CR_AUTO); /* after the PHY: the MAC follows its speed */
	reg_write(MAC_TX, MAC_TX_TXEN);
	reg_write(FCT_TX_CTL, FCT_EN);
	reg_write(MAC_RX, (FRAME_MAX + 4u + 4u) << 16 | MAC_RX_FCS_STRIP | MAC_RX_RXEN);
	reg_write(FCT_RX_CTL, FCT_EN);
	return true;
}

/* send one Ethernet frame (destination MAC first, no FCS); 0 or an error */
int lan78xx_send(const void *frame, uint32_t len) {
	static uint8_t b[8 + FRAME_MAX];
	if (len > FRAME_MAX - 4u)
		return -2;
	uint32_t n = len < 60u ? 60u : len; /* pad to the Ethernet minimum */
	uint32_t a = n | TX_CMD_A_FCS;
	b[0] = (uint8_t)a, b[1] = (uint8_t)(a >> 8), b[2] = (uint8_t)(a >> 16), b[3] = (uint8_t)(a >> 24);
	b[4] = b[5] = b[6] = b[7] = 0;
	memcpy(b + 8, frame, len);
	for (uint32_t i = len; i < n; i++)
		b[8 + i] = 0;
	int r = usb_bulk(&g_out, false, &g_tog_out, b, 8u + n);
	return r < 0 ? r : 0;
}

/*
 * One receive try: the frame's length (copied to buf, at most max bytes),
 * 0 if none is waiting, or a negative error.
 */
int lan78xx_recv(void *buf, uint32_t max) {
	static uint8_t b[2048];
	int n = usb_bulk_in_try(&g_in, &g_tog_in, b, sizeof b);
	if (n <= 0)
		return n;
	if ((uint32_t)n < RX_HDR)
		return -9;
	uint32_t a = (uint32_t)(b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24);
	uint32_t len = a & 0x3fffu;
	if (a & RX_CMD_A_RXE)
		return -10;
	if (len > (uint32_t)n - RX_HDR)
		return -9;
	if (len > max)
		len = max;
	memcpy(buf, b + RX_HDR, len);
	return (int)len;
}

/*
 * Monitor check (N7): bring the chip up, send an ARP probe for target_ip
 * (RFC 5227: our IP 0.0.0.0, so the answer comes to our MAC), and listen
 * for 3 s: count the frames, print the first few and the ARP reply.
 */
bool lan78xx_check(uint32_t target_ip) {
	if (!usb_probe_root())
		return false;
	const struct usb_dev *dev = usb_find(0x0424, 0x7800);
	if (!dev) {
		printf("err eth: no LAN7800 (0424:7800) found\n");
		return false;
	}
	if (!lan78xx_start(dev, 5000))
		return false;
	uint8_t arp[42] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	memcpy(arp + 6, g_mac, 6);
	static const uint8_t hdr[10] = {0x08, 0x06, 0x00, 0x01, 0x08, 0x00, 6, 4, 0x00, 0x01};
	memcpy(arp + 12, hdr, 10);
	memcpy(arp + 22, g_mac, 6); /* sender MAC; sender IP 0.0.0.0, target MAC 0 */
	arp[38] = (uint8_t)(target_ip >> 24), arp[39] = (uint8_t)(target_ip >> 16);
	arp[40] = (uint8_t)(target_ip >> 8), arp[41] = (uint8_t)target_ip;
	uint32_t frames = 0, bytes = 0, errs = 0, sent = 0;
	bool answered = false;
	uint32_t t0 = timer_us(), last_tx = 0;
	static uint8_t f[FRAME_MAX];
	while (timer_us() - t0 < 3000000u) {
		uint32_t now = timer_us() - t0;
		if (target_ip && !answered && (sent == 0 || now - last_tx > 1000000u)) {
			int r = lan78xx_send(arp, sizeof arp);
			if (r < 0) {
				printf("err eth: send failed: %d\n", r);
				return false;
			}
			sent++;
			last_tx = now;
		}
		int n = lan78xx_recv(f, sizeof f);
		if (n < 0) {
			errs++;
			continue;
		}
		if (n == 0) {
			udelay(200);
			continue;
		}
		frames++;
		bytes += (uint32_t)n;
		uint32_t type = n >= 14 ? (uint32_t)(f[12] << 8 | f[13]) : 0;
		if (frames <= 5)
			printf("eth: rx %d bytes, type %04X, from %02x:%02x:%02x:%02x:%02x:%02x\n", n, type,
			       f[6], f[7], f[8], f[9], f[10], f[11]);
		if (type == 0x0806 && n >= 42 && f[21] == 2 && f[28] == arp[38] && f[29] == arp[39] &&
		    f[30] == arp[40] && f[31] == arp[41] && !answered) {
			answered = true;
			printf("eth: ARP reply: %u.%u.%u.%u is at %02x:%02x:%02x:%02x:%02x:%02x\n", f[28], f[29],
			       f[30], f[31], f[22], f[23], f[24], f[25], f[26], f[27]);
		}
	}
	printf("eth: 3 s: %u ARP probe(s) sent, %u frames (%u bytes) received, %u receive errors\n", sent,
	       frames, bytes, errs);
	if (target_ip && !answered) {
		printf("err eth: no ARP reply\n");
		return false;
	}
	return true;
}
