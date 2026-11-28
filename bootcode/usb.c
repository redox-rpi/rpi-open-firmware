/*
 * usb.c: the DWC2 USB controller as a host, for network boot (rof #116, N1).
 *
 * Core reset, host mode, root port power and reset, and transfers on one
 * host channel in DMA mode: control transfers now, bulk for the network
 * driver (N3). No split transactions: on both boards everything netboot
 * needs is high speed (the LAN7515 hub and LAN7800 on the 3B+, the HAT's
 * hub and RTL8152 on the Zero 2 W).
 *
 * The register sequence follows U-Boot's drivers/usb/host/dwc2.c
 * (GPL-2.0+), trimmed to the BCM283x configuration: UTMI+ PHY 8 bit,
 * internal DMA, the FIFO sizes the controller comes up with.
 *
 * DMA buffers are in the VideoCore's part of the SDRAM (USB_DMA_OFF), seen
 * through the uncached alias by both the VPU and the controller, so no cache
 * maintenance is needed.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"
#include "usb.h"
#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n); /* libc.c */

#define D(off) HW_REGISTER_RW(0x7e980000u + (off))
#define GAHBCFG D(0x008)
#define GUSBCFG D(0x00c)
#define GRSTCTL D(0x010)
#define GINTSTS D(0x014)
#define GSNPSID D(0x040)
#define GHWCFG2 D(0x048)
#define HCFG D(0x400)
#define HPRT0 D(0x440)
#define PCGCCTL D(0xe00)
#define HCCHAR(n) D(0x500 + 0x20 * (n))
#define HCSPLT(n) D(0x504 + 0x20 * (n))
#define HCINT(n) D(0x508 + 0x20 * (n))
#define HCINTMSK(n) D(0x50c + 0x20 * (n))
#define HCTSIZ(n) D(0x510 + 0x20 * (n))
#define HCDMA(n) D(0x514 + 0x20 * (n))

#define GRSTCTL_CSFTRST (1u << 0)
#define GRSTCTL_RXFFLSH (1u << 4)
#define GRSTCTL_TXFFLSH (1u << 5)
#define GRSTCTL_TXFNUM_ALL (0x10u << 6)
#define GRSTCTL_AHBIDLE (1u << 31)
#define GAHBCFG_DMA_INCR4 (1u << 5 | 3u << 1) /* DMA enable, burst INCR4 */
#define GUSBCFG_PHYIF16 (1u << 3)
#define GUSBCFG_ULPI_UTMI_SEL (1u << 4)
#define GUSBCFG_FORCEHOSTMODE (1u << 29)
#define GUSBCFG_FORCEDEVMODE (1u << 30)
#define HPRT0_CONNSTS (1u << 0)
#define HPRT0_W1C 0x2eu /* CONNDET, ENA, ENACHG, OVRCURRCHG: write 1 to clear */
#define HPRT0_ENA (1u << 2)
#define HPRT0_RST (1u << 8)
#define HPRT0_PWR (1u << 12)
#define HPRT0_SPD(v) (((v) >> 17) & 3u)
#define HCCHAR_EPDIR_IN (1u << 15)
#define HCCHAR_CHDIS (1u << 30)
#define HCCHAR_CHENA (1u << 31)
#define HCINT_XFERCOMPL (1u << 0)
#define HCINT_CHHLTD (1u << 1)
#define HCINT_STALL (1u << 3)
#define HCINT_NAK (1u << 4)
#define HCINT_FRMOVRUN (1u << 9)
#define PID_DATA0 0u
#define PID_DATA1 2u
#define PID_SETUP 3u

#define CH 0 /* one host channel is enough for a boot loader */
#define USB_DMA_OFF 0x300000u /* 256 KB in the VideoCore's part, after diag.c's ring */
#define USB_DMA_BYTES 0x40000u

static uint8_t *dma_buf(void) {
	return (uint8_t *)(ALIAS_UNCACHED | (g_arm_mem + USB_DMA_OFF));
}

static bool wait_clear(volatile unsigned int *reg, uint32_t bits, uint32_t us) {
	uint32_t t0 = timer_us();
	while (*reg & bits)
		if (timer_us() - t0 > us)
			return false;
	return true;
}

static bool wait_set(volatile unsigned int *reg, uint32_t bits, uint32_t us) {
	uint32_t t0 = timer_us();
	while ((*reg & bits) != bits)
		if (timer_us() - t0 > us)
			return false;
	return true;
}

static bool core_reset(void) {
	if (!wait_set(&GRSTCTL, GRSTCTL_AHBIDLE, 100000))
		return false;
	GRSTCTL |= GRSTCTL_CSFTRST;
	if (!wait_clear(&GRSTCTL, GRSTCTL_CSFTRST, 100000))
		return false;
	udelay(100000); /* the core needs some time after reset (U-Boot: 100 ms) */
	return true;
}

static void flush_fifos(void) {
	GRSTCTL = GRSTCTL_TXFFLSH | GRSTCTL_TXFNUM_ALL;
	wait_clear(&GRSTCTL, GRSTCTL_TXFFLSH, 10000);
	udelay(1);
	GRSTCTL = GRSTCTL_RXFFLSH;
	wait_clear(&GRSTCTL, GRSTCTL_RXFFLSH, 10000);
	udelay(1);
}

static uint32_t g_speed; /* root port: 0 high, 1 full, 2 low speed */

/* core and root port up, the device on the root port reset; true when one is there */
bool usb_host_start(void) {
	uint32_t id = GSNPSID;
	if ((id & 0xfffff000u) != 0x4f542000u) {
		printf("err usb: no DWC2 core (GSNPSID %08X)\n", id);
		return false;
	}
	uint32_t cfg = GUSBCFG;
	cfg &= ~(GUSBCFG_ULPI_UTMI_SEL | GUSBCFG_PHYIF16); /* UTMI+, 8 bit */
	cfg &= ~GUSBCFG_FORCEDEVMODE;
	cfg |= GUSBCFG_FORCEHOSTMODE;
	GUSBCFG = cfg;
	if (!core_reset()) {
		printf("err usb: core reset timed out (GRSTCTL %08X)\n", GRSTCTL);
		return false;
	}
	if (((GHWCFG2 >> 3) & 3u) != 2u) { /* architecture: internal DMA */
		printf("err usb: no internal DMA (GHWCFG2 %08X)\n", GHWCFG2);
		return false;
	}
	GAHBCFG = GAHBCFG_DMA_INCR4; /* no interrupts: we poll */
	udelay(50000); /* forced host mode takes effect after up to 25 ms */
	if (!(GINTSTS & 1u)) {
		printf("err usb: core not in host mode (GINTSTS %08X)\n", GINTSTS);
		return false;
	}
	PCGCCTL = 0;
	HCFG = (HCFG & ~3u); /* PHY clock 30/60 MHz */
	flush_fifos();
	uint32_t nch = ((GHWCFG2 >> 14) & 0xfu) + 1u;
	for (uint32_t i = 0; i < nch; i++) {
		HCCHAR(i) = (HCCHAR(i) & ~(HCCHAR_CHENA | HCCHAR_EPDIR_IN)) | HCCHAR_CHDIS;
		HCCHAR(i) = (HCCHAR(i) & ~HCCHAR_EPDIR_IN) | HCCHAR_CHENA | HCCHAR_CHDIS;
		wait_clear(&HCCHAR(i), HCCHAR_CHENA, 10000);
	}
	uint32_t p = HPRT0 & ~HPRT0_W1C;
	if (!(p & HPRT0_PWR))
		HPRT0 = p | HPRT0_PWR;
	/* wait for a device to connect */
	if (!wait_set(&HPRT0, HPRT0_CONNSTS, 1000000)) {
		printf("usb: nothing on the root port (HPRT0 %08X)\n", HPRT0);
		return false;
	}
	udelay(100000); /* USB 2.0: 100 ms debounce after connect */
	p = HPRT0 & ~HPRT0_W1C;
	HPRT0 = p | HPRT0_RST;
	udelay(50000);
	HPRT0 = p & ~HPRT0_RST;
	udelay(20000);
	if (!(HPRT0 & HPRT0_ENA)) {
		printf("err usb: root port not enabled after reset (HPRT0 %08X)\n", HPRT0);
		return false;
	}
	g_speed = HPRT0_SPD(HPRT0);
	printf("usb: DWC2 %X.%03X, %u channels, device on the root port, %s speed\n",
	       (id >> 12) & 0xfu, id & 0xfffu, nch,
	       g_speed == 0 ? "high" : g_speed == 1 ? "full" : "low");
	return true;
}

/* one transfer on the channel; returns the bytes moved or a negative error */
static int xfer(const struct usb_ep *ep, bool in, uint32_t *pid, void *buf, uint32_t len) {
	uint8_t *d = dma_buf();
	uint32_t mps = ep->mps ? ep->mps : 64u;
	if (len > USB_DMA_BYTES)
		return -2;
	uint32_t pkts = len ? (len + mps - 1u) / mps : 1u;
	if (!in && len)
		memcpy(d, buf, len);
	for (int tries = 0; tries < 2000; tries++) {
		HCCHAR(CH) = (mps & 0x7ffu) | (ep->ep & 0xfu) << 11 | (in ? HCCHAR_EPDIR_IN : 0) |
		             (ep->type & 3u) << 18 | 1u << 20 | (ep->addr & 0x7fu) << 22;
		HCSPLT(CH) = 0;
		HCTSIZ(CH) = (len & 0x7ffffu) | (pkts & 0x3ffu) << 19 | (*pid & 3u) << 29;
		HCDMA(CH) = (uint32_t)d;
		HCINT(CH) = 0x3fffu;
		HCINTMSK(CH) = 0;
		HCCHAR(CH) |= HCCHAR_CHENA;
		if (!wait_set(&HCINT(CH), HCINT_CHHLTD, 500000)) {
			HCCHAR(CH) |= HCCHAR_CHDIS;
			return -3; /* timeout */
		}
		uint32_t st = HCINT(CH), ts = HCTSIZ(CH);
		if (st & HCINT_XFERCOMPL) {
			*pid = (ts >> 29) & 3u;
			uint32_t n = in ? len - (ts & 0x7ffffu) : len;
			if (in && n)
				memcpy(buf, d, n);
			return (int)n;
		}
		if (st & HCINT_STALL)
			return -4;
		if (!(st & (HCINT_NAK | HCINT_FRMOVRUN)))
			return -5; /* transaction error, babble, data toggle */
		udelay(100); /* NAK: the device is not ready, ask again */
	}
	return -6;
}

int usb_control(const struct usb_ep *ep0, const struct usb_setup *s, void *data) {
	uint32_t pid = PID_SETUP;
	int r = xfer(ep0, false, &pid, (void *)s, 8);
	if (r < 0)
		return r;
	bool in = s->bmRequestType & 0x80u;
	int n = 0;
	if (s->wLength) {
		pid = PID_DATA1;
		n = xfer(ep0, in, &pid, data, s->wLength);
		if (n < 0)
			return n;
	}
	pid = PID_DATA1; /* status stage: the other direction, no data */
	r = xfer(ep0, s->wLength ? !in : true, &pid, 0, 0);
	return r < 0 ? r : n;
}

int usb_bulk(const struct usb_ep *ep, bool in, uint32_t *toggle, void *buf, uint32_t len) {
	return xfer(ep, in, toggle, buf, len);
}

uint32_t usb_root_speed(void) {
	return g_speed;
}

/* N1 check: enumerate the device on the root port and print its descriptor */
bool usb_probe_root(void) {
	if (!usb_host_start())
		return false;
	struct usb_ep ep0 = {.addr = 0, .ep = 0, .type = USB_EP_CONTROL, .mps = 64};
	uint8_t dd[18];
	struct usb_setup get = {0x80, 6, 0x0100, 0, 8}; /* GET_DESCRIPTOR device, 8 bytes */
	int n = usb_control(&ep0, &get, dd);
	if (n < 8) {
		printf("err usb: device descriptor (8 bytes) failed: %d\n", n);
		return false;
	}
	ep0.mps = dd[7];
	struct usb_setup addr = {0x00, 5, 1, 0, 0}; /* SET_ADDRESS 1 */
	n = usb_control(&ep0, &addr, 0);
	if (n < 0) {
		printf("err usb: SET_ADDRESS failed: %d\n", n);
		return false;
	}
	udelay(10000);
	ep0.addr = 1;
	get.wLength = 18;
	n = usb_control(&ep0, &get, dd);
	if (n < 18) {
		printf("err usb: device descriptor (18 bytes) failed: %d\n", n);
		return false;
	}
	printf("usb: device %04X:%04X, USB %X.%02X, class %02X, max packet %u, %u configuration(s)\n",
	       dd[8] | dd[9] << 8, dd[10] | dd[11] << 8, dd[3], dd[2], dd[4], dd[7], dd[17]);
	return true;
}
