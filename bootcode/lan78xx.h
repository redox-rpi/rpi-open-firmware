/*
 * lan78xx.h: the Pi 3B+'s LAN7800 Ethernet chip for network boot (rof #122).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef LAN78XX_H
#define LAN78XX_H
#include "usb.h"
#include <stdbool.h>
#include <stdint.h>

bool lan78xx_start(const struct usb_dev *dev, uint32_t link_ms);
const uint8_t *lan78xx_mac(void);
int lan78xx_send(const void *frame, uint32_t len);
int lan78xx_recv(void *buf, uint32_t max);
bool lan78xx_check(uint32_t target_ip);

#endif
