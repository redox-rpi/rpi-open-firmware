/*
 * redox-rpi bootcode: the config.txt keys a direct boot needs (rof #48),
 * so a card made for Broadcom's firmware boots with ours unchanged.
 *
 * Keys read (the rest is ignored, as start.elf ignores what it does not
 * know):
 *   arm_64bit=1          only 64-bit is supported; 0 gives a message
 *   kernel=NAME          default kernel8.img
 *   initramfs NAME [followkernel|ADDR], ramfsfile=NAME
 *                        the place is ours (after the devicetree); an
 *                        address is ignored with a message
 *   armstub=NAME         default: the built-in armstub8
 *   device_tree=NAME     default by board; empty: no devicetree
 *   dtoverlay=NAME[,...] up to CFG_OVERLAYS, from overlays/NAME.dtbo;
 *                        parameters are not supported (a message)
 *   dtparam=...          not supported (a message)
 *   cmdline=NAME         default cmdline.txt
 *
 * Filters: a [section] line decides whether the lines after it apply.
 * [all] applies everything again. A model filter ([pi3], [pi3+], [pi02],
 * [pi0], [board-type=0xN]) replaces the previous model filter, a GPIO
 * filter ([gpioN=0|1]) the previous GPIO filter, and the two combine. Any
 * other filter ([pi4], [tryboot], [HDMI:1], [EDID=...], [0xSERIAL], ...)
 * counts as false until the next [all], and [none] is false too. include
 * and os_prefix are not followed (a message).
 *
 * Model filters as Raspberry Pi's documentation of config.txt filters:
 * [pi3] = 3B, 3B+, 3A+, CM3, CM3+; [pi3+] = 3B+, 3A+; [pi0] = Zero, Zero W,
 * Zero 2 W; [pi02] = Zero 2 W. Board types are bits 4..11 of the board
 * revision.
 *
 * Pure parsing: the caller passes the board type and GPIO levels, so the
 * host test (tests/config-test.sh) runs it unchanged. New code.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"

/* a message: prefix, n bytes of w, rest, newline (printf has no %.*s) */
static void note(const char *prefix, const char *w, uint32_t n, const char *rest) {
	printf("config: %s", prefix);
	for (uint32_t i = 0; i < n; i++)
		printf("%c", w[i]);
	printf("%s\n", rest);
}

static bool is_sp(char c) {
	return c == ' ' || c == '\t';
}

static uint32_t clen(const char *s) {
	uint32_t n = 0;
	while (s[n])
		n++;
	return n;
}

/* the word w (n bytes) equals s */
static bool word_is(const char *w, uint32_t n, const char *s) {
	uint32_t i = 0;
	for (; i < n; i++)
		if (w[i] != s[i])
			return false;
	return s[i] == 0;
}

static void copy_word(char *d, uint32_t cap, const char *w, uint32_t n, const char *key) {
	if (n >= cap) {
		printf("config: %s value longer than %u bytes, ignored\n", key, cap - 1);
		return;
	}
	for (uint32_t i = 0; i < n; i++)
		d[i] = w[i];
	d[n] = 0;
}

/* a number: decimal or 0x hex; false when w is not one */
static bool number(const char *w, uint32_t n, uint32_t *out) {
	uint32_t v = 0, i = 0, base = 10;
	if (n > 2 && w[0] == '0' && (w[1] == 'x' || w[1] == 'X'))
		base = 16, i = 2;
	if (i == n)
		return false;
	for (; i < n; i++) {
		char c = w[i];
		uint32_t d;
		if (c >= '0' && c <= '9')
			d = (uint32_t)(c - '0');
		else if (base == 16 && c >= 'a' && c <= 'f')
			d = (uint32_t)(c - 'a' + 10);
		else if (base == 16 && c >= 'A' && c <= 'F')
			d = (uint32_t)(c - 'A' + 10);
		else
			return false;
		v = v * base + d;
	}
	*out = v;
	return true;
}

static bool model_filter(const char *f, uint32_t n, uint32_t type, bool *known) {
	*known = true;
	uint32_t bt;
	if (n > 11 && word_is(f, 11, "board-type=") && number(f + 11, n - 11, &bt))
		return type == bt;
	if (word_is(f, n, "pi3"))
		return type == 0x8 || type == 0xd || type == 0xe || type == 0xa || type == 0x10;
	if (word_is(f, n, "pi3+"))
		return type == 0xd || type == 0xe;
	if (word_is(f, n, "pi0"))
		return type == 0x9 || type == 0xc || type == 0x12;
	if (word_is(f, n, "pi02"))
		return type == 0x12;
	if (word_is(f, n, "pi1") || word_is(f, n, "pi2") || word_is(f, n, "pi4") || word_is(f, n, "pi400") ||
	    word_is(f, n, "pi5") || word_is(f, n, "pi0w") || word_is(f, n, "cm4"))
		return false;
	*known = false;
	return false;
}

/* [gpioN=V]: true/false in *v; false when f is not a GPIO filter */
static bool gpio_filter(const char *f, uint32_t n, const uint32_t lev[2], bool *v) {
	if (n < 7 || !word_is(f, 4, "gpio"))
		return false;
	uint32_t i = 4, pin = 0, want;
	while (i < n && f[i] >= '0' && f[i] <= '9')
		pin = pin * 10 + (uint32_t)(f[i++] - '0');
	if (i == 4 || i >= n || f[i] != '=' || pin > 53 || !number(f + i + 1, n - i - 1, &want))
		return false;
	*v = ((lev[pin / 32] >> (pin % 32)) & 1u) == (want ? 1u : 0u);
	return true;
}

void cfg_parse(struct boot_cfg *c, const char *txt, uint32_t len, uint32_t board_type, uint32_t lev0, uint32_t lev1) {
	const uint32_t lev[2] = {lev0, lev1};
	const char *p = txt, *end = txt + len;
	bool model = true, gpio = true, other = true;
	while (p < end) {
		const char *line = p;
		while (p < end && *p != '\n' && *p != '\r')
			p++;
		const char *le = p;
		while (p < end && (*p == '\n' || *p == '\r'))
			p++;
		while (line < le && is_sp(*line))
			line++;
		while (le > line && is_sp(le[-1]))
			le--;
		if (line == le || *line == '#')
			continue;
		if (*line == '[') {
			const char *f = line + 1, *fe = f;
			while (fe < le && *fe != ']')
				fe++;
			uint32_t n = (uint32_t)(fe - f);
			bool known, v;
			if (word_is(f, n, "all")) {
				model = gpio = other = true;
			} else if (word_is(f, n, "none")) {
				other = false;
			} else if (gpio_filter(f, n, lev, &v)) {
				gpio = v;
			} else {
				v = model_filter(f, n, board_type, &known);
				if (known) {
					model = v;
				} else {
					other = false;
					c->unknown_filters++;
				}
			}
			continue;
		}
		if (!(model && gpio && other))
			continue;
		/* key=value, or "initramfs NAME [where]" */
		const char *k = line;
		while (line < le && *line != '=' && !is_sp(*line))
			line++;
		uint32_t kn = (uint32_t)(line - k);
		while (line < le && is_sp(*line))
			line++;
		bool eq = line < le && *line == '=';
		if (eq)
			line++;
		while (line < le && is_sp(*line))
			line++;
		const char *v = line;
		uint32_t vn = (uint32_t)(le - v);
		if (eq && word_is(k, kn, "arm_64bit")) {
			uint32_t x;
			c->arm_64bit = !number(v, vn, &x) || x != 0;
		} else if (eq && word_is(k, kn, "kernel")) {
			copy_word(c->kernel, sizeof c->kernel, v, vn, "kernel");
		} else if (eq && word_is(k, kn, "ramfsfile")) {
			copy_word(c->initramfs, sizeof c->initramfs, v, vn, "ramfsfile");
		} else if (!eq && word_is(k, kn, "initramfs")) {
			uint32_t n = 0;
			while (n < vn && !is_sp(v[n]))
				n++;
			copy_word(c->initramfs, sizeof c->initramfs, v, n, "initramfs");
			const char *w = v + n;
			while (w < le && is_sp(*w))
				w++;
			if (w < le && !word_is(w, (uint32_t)(le - w), "followkernel"))
				printf("config: initramfs address ignored, it goes after the devicetree\n");
		} else if (eq && word_is(k, kn, "armstub")) {
			copy_word(c->armstub, sizeof c->armstub, v, vn, "armstub");
		} else if (eq && word_is(k, kn, "device_tree")) {
			copy_word(c->device_tree, sizeof c->device_tree, v, vn, "device_tree");
			c->device_tree_set = true;
		} else if (eq && word_is(k, kn, "cmdline")) {
			copy_word(c->cmdline, sizeof c->cmdline, v, vn, "cmdline");
		} else if (eq && word_is(k, kn, "dtoverlay")) {
			uint32_t n = 0;
			while (n < vn && v[n] != ',' && v[n] != ':')
				n++;
			if (n < vn)
				note("dtoverlay parameters not supported: ", v, vn, "");
			if (!n)
				continue;
			if (c->n_overlays == CFG_OVERLAYS) {
				note("too many dtoverlay lines, ignored: ", v, n, "");
				continue;
			}
			copy_word(c->overlay[c->n_overlays], sizeof c->overlay[0], v, n, "dtoverlay");
			if (c->overlay[c->n_overlays][0])
				c->n_overlays++;
		} else if (eq && word_is(k, kn, "dtparam")) {
			note("dtparam not supported: ", v, vn, "");
		} else if (word_is(k, kn, "include") || word_is(k, kn, "os_prefix")) {
			note("", k, kn, " not supported, ignored");
		}
	}
	if (!c->kernel[0])
		copy_word(c->kernel, sizeof c->kernel, "kernel8.img", clen("kernel8.img"), "kernel");
	if (!c->cmdline[0])
		copy_word(c->cmdline, sizeof c->cmdline, "cmdline.txt", clen("cmdline.txt"), "cmdline");
}

void cfg_defaults(struct boot_cfg *c) {
	char *b = (char *)c;
	for (uint32_t i = 0; i < sizeof *c; i++)
		b[i] = 0;
	c->arm_64bit = true;
}

/* Broadcom's devicetree for the board, as start.elf picks it */
const char *cfg_board_dtb(uint32_t board_type) {
	if (board_type == 0x8)
		return "bcm2710-rpi-3-b.dtb";
	if (board_type == 0xa || board_type == 0x10)
		return "bcm2710-rpi-cm3.dtb";
	if (board_type == 0x12)
		return "bcm2710-rpi-zero-2-w.dtb";
	return "bcm2710-rpi-3-b-plus.dtb"; /* 3B+, 3A+ */
}
