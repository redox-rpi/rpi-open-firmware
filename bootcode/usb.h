/*
 * usb.h: DWC2 USB host for network boot (rof #116, #117).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef USB_H
#define USB_H
#include <stdbool.h>
#include <stdint.h>

#define USB_EP_CONTROL 0u
#define USB_EP_BULK 2u

struct usb_ep {
	uint8_t addr; /* device address */
	uint8_t ep;   /* endpoint number */
	uint8_t type; /* USB_EP_CONTROL or USB_EP_BULK */
	uint16_t mps; /* max packet size */
};

struct usb_setup {
	uint8_t bmRequestType;
	uint8_t bRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;
} __attribute__((packed));

struct usb_dev {
	struct usb_ep ep0;
	uint16_t vid, pid;
	uint8_t cls;
};

bool usb_host_start(void);
bool usb_enumerate(uint8_t addr, struct usb_dev *dev);
int usb_set_config(const struct usb_dev *dev, uint8_t cfg);
int usb_hub_scan(const struct usb_dev *hub, uint8_t *next, struct usb_dev *found, int max);
uint32_t usb_root_speed(void); /* 0 high, 1 full, 2 low */
int usb_control(const struct usb_ep *ep0, const struct usb_setup *s, void *data);
int usb_bulk(const struct usb_ep *ep, bool in, uint32_t *toggle, void *buf, uint32_t len);
bool usb_probe_root(void);
int usb_bulk_in_try(const struct usb_ep *ep, uint32_t *toggle, void *buf, uint32_t len);
bool usb_bulk_endpoints(const struct usb_dev *dev, struct usb_ep *in, struct usb_ep *out);
const struct usb_dev *usb_find(uint16_t vid, uint16_t pid);
void usb_stats(uint32_t *xfers, uint32_t *odd);

#endif
