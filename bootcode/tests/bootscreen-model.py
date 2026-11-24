#!/usr/bin/env python3
# The boot screen panel (#430) as bootscreen.c draws it, for
# bootscreen-sim.sh: the band sums the resim test compares, and the CRC-32
# the monitor's bs command prints on the Pi. The font comes from
# font8x16.h. SPDX-License-Identifier: GPL-2.0-or-later
import re, struct, sys, zlib

W, H = 640, 160
BLACK, WHITE, GREY, RED, BORDER, EMPTY, BAR = 0x0000, 0xffff, 0xa514, 0xfa08, 0x8410, 0x3186, 0xfc00
LINES, LINE_MAX = 4, W // 8

font_h = open(sys.argv[1]).read()
font = [int(v, 16) for v in re.findall(r'0x([0-9a-f]{2})', font_h[font_h.index('g_font['):])]
assert len(font) == 95 * 16

TITLE = "Booting Raspberry Pi ..."

def render(lines, pct, title=TITLE):
    """lines: [(text, fail)] as on screen (the last LINES), pct: the bar"""
    fb = [[BLACK] * W for _ in range(H)]
    def text(x, y, s, sh, fg, x_end):
        cw = 8 << sh
        for row in range(16 << sh):
            px = x
            for ch in s:
                if px + cw > x_end:
                    break
                c = ord(ch)
                bits = font[(c - 32) * 16 + (row >> sh)] if 32 <= c <= 126 else 0
                for b in range(cw):
                    fb[y + row][px] = fg if bits & (0x80 >> (b >> sh)) else BLACK
                    px += 1
    tw = len(title) * 16
    x = (W - tw) // 2 & ~1
    text(x, 0, title, 1, WHITE, x + tw)
    inner = W - 4
    done = (inner * pct // 100) & ~1
    for y in range(48, 64):
        for x in range(W):
            edge = y < 50 or y >= 62 or x < 2 or x >= W - 2
            fb[y][x] = BORDER if edge else BAR if x - 2 < done else EMPTY
    for i, (s, fail) in enumerate(lines):
        fg = RED if fail else WHITE if i == len(lines) - 1 else GREY
        text(0, 80 + i * 20, s, 0, fg, W)
    return b"".join(struct.pack("<%dH" % W, *row) for row in fb)

def sums(raw):
    words = struct.unpack("<%dI" % (len(raw) // 4), raw)
    band_words = W // 2 * 16
    out, a1, a2 = [], 0, 0
    for band in range(H // 16):
        s1 = s2 = 0
        for w in words[band * band_words:(band + 1) * band_words]:
            s1 = (s1 + w) & 0xffffffff
            s2 = (s2 + s1) & 0xffffffff
            a1 = (a1 + w) & 0xffffffff
            a2 = (a2 + a1) & 0xffffffff
        out.append("%08X" % s2)
    return " ".join(out) + " all %08X %08X" % (a1, a2)

FW_SHARE = 40  # bootscreen.c BS_FW_SHARE: the firmware's pct 0..90 fill 0..40 %

def fw_bar(p):
    return FW_SHARE if p >= 90 else p * FW_SHARE // 90

def steps(seq, title=TITLE):
    """seq: [(pct or None, text, fail)] as bs_start/bs_step/bs_fail"""
    lines, pct = [], 0
    for i, (p, s, fail) in enumerate(seq):
        lines.append((s[:LINE_MAX], fail))
        lines = lines[-LINES:]
        if i == 0:
            pct = fw_bar(p)
        elif p is not None and fw_bar(p) > pct:
            pct = fw_bar(p)
    return render(lines, pct, title)

demo = steps([
    (10, "bootcode test, SDRAM ready", False),
    (20, "SD card SC64G", False),
    (30, "loading armstub8-redox.bin", False),
    (45, "loading kernel8.img", False),
    (60, "loading redox-3bp.dtb", False),
    (75, "starting the ARM", False),
    (None, "a failed step looks like this", True),
    (90, "ARM running", False),
])
long_seq = [
    (0, "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789abcdefghijklmnopqrstuvwxyz", False),
    (None, "no SD card", True),
]
long = steps(long_seq)
# boot_title= from config.txt (bootscreen-sim-main.c's text)
titled = steps(long_seq, "Redox on Raspberry Pi")

if sys.argv[2:] == ["crc"]:
    print("%08X" % zlib.crc32(demo))
else:
    print("RESULT demo " + sums(demo))
    print("RESULT handover " + sums(demo))
    print("RESULT long " + sums(long))
    print("RESULT title " + sums(titled))
