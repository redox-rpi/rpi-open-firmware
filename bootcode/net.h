/*
 * net.h: ARP, IPv4, UDP, DHCP and TFTP for network boot (rof #119, #120).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef NET_H
#define NET_H
#include <stdbool.h>
#include <stdint.h>

bool net_dhcp(void);
int net_tftp(uint32_t server, const char *file, uint8_t *dst, uint32_t max, uint32_t blksize,
             uint32_t window);
uint32_t net_parse_ip(const char *s, bool *ok);
void net_print_ip(uint32_t ip);

#endif
