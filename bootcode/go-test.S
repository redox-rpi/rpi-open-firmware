/*
 * redox-rpi bootcode: smallest payload for the monitor's "g" command.
 * Position independent; returns r0 + r1 + 0x1000.
 *   rbc-load.sh build/go-test.bin 0xc1000000 none
 *   then on the monitor: g c1000000 5 7   ->   ok ret 0000100C
 *
 * New code. SPDX-License-Identifier: GPL-2.0-or-later
 */
.text
.globl go_test
go_test:
	add r0, r1
	add r0, 0x1000
	rts
