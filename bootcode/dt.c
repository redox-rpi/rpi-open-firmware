/*
 * redox-rpi bootcode: edit a flattened devicetree in a buffer with room to
 * grow (rof #48): look nodes up by path or phandle, set properties (new
 * ones, or a new length), add nodes, and apply overlays (.dtbo) the way
 * Broadcom's start.elf applies config.txt's dtoverlay= lines. fdt.c only
 * overwrites values in place; this is for the standard release card, which
 * has Broadcom's devicetree and overlays/ instead of a merged redox-*.dtb.
 *
 * A node or property is named by its offset in the blob. Every change that
 * grows the blob moves what comes after the change, so offsets past it are
 * stale afterwards; look them up again.
 *
 * The blob must have its structure block before its strings block (dtc
 * writes them so). A new property name goes at the end of the strings.
 *
 * Overlays: __fixups__ (labels of the base, from its __symbols__) and
 * __local_fixups__ (the overlay's own phandles, moved past the base's
 * highest) are resolved as in libfdt's fdt_overlay_apply, then each
 * fragment's __overlay__ is merged into its target (target = <phandle> or
 * target-path = "/path"). A "phandle" in an __overlay__ node is dropped
 * when the target already has one. __overrides__ (overlay parameters) and
 * the overlay's __symbols__ are not used.
 *
 * New code, written from the Devicetree Specification v0.4, chapter 5, and
 * the overlay format as dtc's Documentation/dt-object-internal.txt
 * describes it.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"

#define FDT_MAGIC 0xd00dfeedu
#define T_BEGIN 1u
#define T_END 2u
#define T_PROP 3u
#define T_NOP 4u
#define T_DONE 9u

#define H_TOTAL 4
#define H_STRUCT 8
#define H_STRINGS 12
#define H_SIZE_STRINGS 32
#define H_SIZE_STRUCT 36

static uint32_t rd(const uint8_t *p) {
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void wr(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static uint32_t align4(uint32_t n) {
	return (n + 3u) & ~3u;
}

static uint32_t slen(const char *s) {
	uint32_t n = 0;
	while (s[n])
		n++;
	return n;
}

static bool seq(const char *a, const char *b) {
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

/* n bytes from s to d, overlapping either way */
static void move(uint8_t *d, const uint8_t *s, uint32_t n) {
	if (d < s)
		for (uint32_t i = 0; i < n; i++)
			d[i] = s[i];
	else
		for (uint32_t i = n; i > 0; i--)
			d[i - 1] = s[i - 1];
}

static uint32_t hdr(const struct dt *t, int off) {
	return rd(t->b + off);
}

/* the end of the strings block, which is the end of what we use */
static uint32_t used(const struct dt *t) {
	return hdr(t, H_STRINGS) + hdr(t, H_SIZE_STRINGS);
}

bool dt_open(struct dt *t, uint8_t *b, uint32_t len, uint32_t cap) {
	t->b = b;
	t->cap = cap;
	if (len < 40 || rd(b) != FDT_MAGIC) {
		printf("err dt: no devicetree magic\n");
		return false;
	}
	uint32_t total = rd(b + H_TOTAL), st = rd(b + H_STRUCT), sst = rd(b + H_SIZE_STRUCT);
	uint32_t str = rd(b + H_STRINGS), sstr = rd(b + H_SIZE_STRINGS);
	if (total > len || total > cap || st + sst > str || str + sstr > total) {
		printf("err dt: unexpected layout (total %u, struct %u+%u, strings %u+%u)\n", total, st, sst, str,
		       sstr);
		return false;
	}
	return true;
}

uint32_t dt_size(const struct dt *t) {
	return hdr(t, H_TOTAL);
}

/* token at off; *next = the offset after it (and after its payload) */
static uint32_t token(const struct dt *t, uint32_t off, uint32_t *next) {
	uint32_t end = hdr(t, H_STRUCT) + hdr(t, H_SIZE_STRUCT);
	if (off + 4 > end) {
		*next = end;
		return T_DONE;
	}
	uint32_t tok = rd(t->b + off);
	off += 4;
	if (tok == T_BEGIN) {
		while (off < end && t->b[off])
			off++;
		off = align4(off + 1);
	} else if (tok == T_PROP) {
		off = align4(off + 8 + rd(t->b + off));
	}
	*next = off;
	return tok;
}

/* the offset after node's T_END */
static uint32_t node_end(const struct dt *t, uint32_t node) {
	uint32_t off = node, next;
	int depth = 0;
	for (;;) {
		uint32_t tok = token(t, off, &next);
		if (tok == T_BEGIN)
			depth++;
		else if (tok == T_END && --depth == 0)
			return next;
		else if (tok == T_DONE)
			return next;
		off = next;
	}
}

static const char *node_name(const struct dt *t, uint32_t node) {
	return (const char *)t->b + node + 4;
}

int32_t dt_root(const struct dt *t) {
	uint32_t off = hdr(t, H_STRUCT), next;
	while (token(t, off, &next) == T_NOP)
		off = next;
	return token(t, off, &next) == T_BEGIN ? (int32_t)off : -1;
}

/* "name" matches "name" and "name@unit"; "name@unit" only itself */
static bool name_is(const char *node, const char *want, uint32_t wn) {
	bool unit = false;
	for (uint32_t i = 0; i < wn; i++) {
		if (node[i] != want[i])
			return false;
		unit |= want[i] == '@';
	}
	return node[wn] == 0 || (node[wn] == '@' && !unit);
}

static int32_t child(const struct dt *t, uint32_t node, const char *name, uint32_t n) {
	uint32_t off, next;
	token(t, node, &off);
	for (;;) {
		uint32_t tok = token(t, off, &next);
		if (tok == T_BEGIN) {
			if (name_is(node_name(t, off), name, n))
				return (int32_t)off;
			next = node_end(t, off);
		} else if (tok == T_END || tok == T_DONE) {
			return -1;
		}
		off = next;
	}
}

int32_t dt_subnode(const struct dt *t, int32_t node, const char *name) {
	return node < 0 ? -1 : child(t, (uint32_t)node, name, slen(name));
}

int32_t dt_path(const struct dt *t, const char *path) {
	int32_t node = dt_root(t);
	if (*path != '/')
		return -1;
	while (node >= 0 && *path) {
		while (*path == '/')
			path++;
		if (!*path)
			break;
		uint32_t n = 0;
		while (path[n] && path[n] != '/')
			n++;
		node = child(t, (uint32_t)node, path, n);
		path += n;
	}
	return node;
}

/* the property's offset (its T_PROP token) in node, or -1 */
static int32_t find_prop(const struct dt *t, uint32_t node, const char *name) {
	uint32_t off, next, strs = hdr(t, H_STRINGS);
	token(t, node, &off);
	for (;;) {
		uint32_t tok = token(t, off, &next);
		if (tok == T_PROP) {
			if (seq((const char *)t->b + strs + rd(t->b + off + 8), name))
				return (int32_t)off;
		} else if (tok != T_NOP) {
			return -1; /* properties come before subnodes */
		}
		off = next;
	}
}

const uint8_t *dt_prop(const struct dt *t, int32_t node, const char *name, uint32_t *len) {
	if (node < 0)
		return 0;
	int32_t p = find_prop(t, (uint32_t)node, name);
	if (p < 0)
		return 0;
	*len = rd(t->b + p + 4);
	return t->b + p + 12;
}

/* make room: n bytes at off (n may be negative to take bytes out) */
static bool grow(struct dt *t, uint32_t off, int32_t n, bool in_struct) {
	uint32_t u = used(t);
	if (n > 0 && u + (uint32_t)n > t->cap) {
		printf("err dt: no room to grow the devicetree past %u bytes\n", t->cap);
		return false;
	}
	move(t->b + off + n, t->b + off, u - off);
	if (in_struct) {
		wr(t->b + H_SIZE_STRUCT, hdr(t, H_SIZE_STRUCT) + (uint32_t)n);
		wr(t->b + H_STRINGS, hdr(t, H_STRINGS) + (uint32_t)n);
	} else {
		wr(t->b + H_SIZE_STRINGS, hdr(t, H_SIZE_STRINGS) + (uint32_t)n);
	}
	wr(t->b + H_TOTAL, used(t));
	return true;
}

/* the offset of name in the strings block, added at its end if missing */
static bool string_off(struct dt *t, const char *name, uint32_t *out) {
	uint32_t strs = hdr(t, H_STRINGS), size = hdr(t, H_SIZE_STRINGS), n = slen(name);
	for (uint32_t i = 0; i + n < size; i++) {
		/* only whole strings: new names are rare */
		if (seq((const char *)t->b + strs + i, name)) {
			*out = i;
			return true;
		}
		while (i < size && t->b[strs + i])
			i++;
	}
	if (!grow(t, strs + size, (int32_t)(n + 1), false))
		return false;
	for (uint32_t i = 0; i <= n; i++)
		t->b[strs + size + i] = (uint8_t)name[i];
	*out = size;
	return true;
}

bool dt_setprop(struct dt *t, int32_t node, const char *name, const void *val, uint32_t len) {
	if (node < 0)
		return false;
	const uint8_t *v = val;
	int32_t p = find_prop(t, (uint32_t)node, name);
	if (p >= 0) {
		uint32_t old = rd(t->b + p + 4);
		int32_t d = (int32_t)align4(len) - (int32_t)align4(old);
		if (d && !grow(t, (uint32_t)p + 12 + align4(old), d, true))
			return false;
		wr(t->b + p + 4, len);
	} else {
		uint32_t nameoff;
		if (!string_off(t, name, &nameoff))
			return false;
		/* after the node's last property: where its first child or its end is */
		uint32_t off, next;
		token(t, (uint32_t)node, &off);
		for (;;) {
			uint32_t tok = token(t, off, &next);
			if (tok != T_PROP && tok != T_NOP)
				break;
			off = next;
		}
		if (!grow(t, off, (int32_t)(12 + align4(len)), true))
			return false;
		p = (int32_t)off;
		wr(t->b + p, T_PROP);
		wr(t->b + p + 4, len);
		wr(t->b + p + 8, nameoff);
	}
	for (uint32_t i = 0; i < align4(len); i++)
		t->b[p + 12 + i] = i < len ? v[i] : 0;
	return true;
}

bool dt_setprop_u32(struct dt *t, int32_t node, const char *name, uint32_t v) {
	uint8_t b[4];
	wr(b, v);
	return dt_setprop(t, node, name, b, 4);
}

bool dt_setprop_str(struct dt *t, int32_t node, const char *name, const char *s) {
	return dt_setprop(t, node, name, s, slen(s) + 1);
}

int32_t dt_add_subnode(struct dt *t, int32_t node, const char *name) {
	if (node < 0)
		return -1;
	int32_t c = dt_subnode(t, node, name);
	if (c >= 0)
		return c;
	uint32_t at = node_end(t, (uint32_t)node) - 4; /* its T_END */
	uint32_t n = slen(name), size = 4 + align4(n + 1) + 4;
	if (!grow(t, at, (int32_t)size, true))
		return -1;
	wr(t->b + at, T_BEGIN);
	for (uint32_t i = 0; i < align4(n + 1); i++)
		t->b[at + 4 + i] = i < n ? (uint8_t)name[i] : 0;
	wr(t->b + at + 4 + align4(n + 1), T_END);
	return (int32_t)at;
}

/* every node; f returns false to stop. For phandle searches. */
static int32_t each_node(const struct dt *t, bool (*f)(const struct dt *, uint32_t, void *), void *arg) {
	uint32_t off = hdr(t, H_STRUCT), next;
	for (;;) {
		uint32_t tok = token(t, off, &next);
		if (tok == T_BEGIN && !f(t, off, arg))
			return (int32_t)off;
		if (tok == T_DONE || next <= off)
			return -1;
		off = next;
	}
}

static uint32_t phandle_of(const struct dt *t, uint32_t node) {
	uint32_t len;
	const uint8_t *v = dt_prop(t, (int32_t)node, "phandle", &len);
	if (!v || len != 4)
		v = dt_prop(t, (int32_t)node, "linux,phandle", &len);
	return v && len == 4 ? rd(v) : 0;
}

struct ph_search {
	uint32_t want, max;
};

static bool ph_find(const struct dt *t, uint32_t node, void *arg) {
	struct ph_search *s = arg;
	uint32_t ph = phandle_of(t, node);
	if (ph > s->max && ph != 0xffffffffu)
		s->max = ph;
	return !s->want || ph != s->want;
}

int32_t dt_by_phandle(const struct dt *t, uint32_t ph) {
	struct ph_search s = {ph, 0};
	return ph ? each_node(t, ph_find, &s) : -1;
}

static uint32_t max_phandle(const struct dt *t) {
	struct ph_search s = {0, 0};
	each_node(t, ph_find, &s);
	return s.max;
}

/* new phandles start above this: the overlay's own, once moved */
static uint32_t g_ph_floor;

/* the node's phandle, given a new one when it has none */
static uint32_t need_phandle(struct dt *t, int32_t node) {
	uint32_t ph = phandle_of(t, (uint32_t)node);
	if (ph)
		return ph;
	ph = max_phandle(t);
	if (ph < g_ph_floor)
		ph = g_ph_floor;
	g_ph_floor = ++ph;
	return dt_setprop_u32(t, node, "phandle", ph) ? ph : 0;
}

/* ----- overlays ----- */

/* add d to the 32-bit cell at byte offset o of property prop at path */
static bool cell_add(struct dt *o, const char *path, const char *prop, uint32_t at, uint32_t d, bool set) {
	int32_t node = dt_path(o, path);
	uint32_t len;
	const uint8_t *v = dt_prop(o, node, prop, &len);
	if (!v || at + 4 > len) {
		printf("err dtoverlay: no cell %u in %s:%s\n", at, path, prop);
		return false;
	}
	uint8_t *c = (uint8_t *)v + at;
	wr(c, set ? d : rd(c) + d);
	return true;
}

/* move the overlay's own phandles past the base's highest (delta) */
static bool local_phandles(struct dt *o, uint32_t delta) {
	uint32_t off = hdr(o, H_STRUCT), next, strs = hdr(o, H_STRINGS);
	for (;;) {
		uint32_t tok = token(o, off, &next);
		if (tok == T_DONE)
			break;
		if (tok == T_PROP && rd(o->b + off + 4) == 4) {
			const char *n = (const char *)o->b + strs + rd(o->b + off + 8);
			if (seq(n, "phandle") || seq(n, "linux,phandle")) {
				uint32_t ph = rd(o->b + off + 12);
				if (ph && ph != 0xffffffffu)
					wr(o->b + off + 12, ph + delta);
			}
		}
		off = next;
	}
	return true;
}

/*
 * __local_fixups__ mirrors the overlay's tree; each property there lists
 * the byte offsets of phandle cells in the same property of the same node.
 * path holds the node's path so far (room for 256 bytes).
 */
static bool local_refs(struct dt *o, uint32_t lf, char *path, uint32_t plen, uint32_t delta) {
	uint32_t off, next, strs = hdr(o, H_STRINGS);
	token(o, lf, &off);
	for (;;) {
		uint32_t tok = token(o, off, &next);
		if (tok == T_PROP) {
			const char *prop = (const char *)o->b + strs + rd(o->b + off + 8);
			uint32_t len = rd(o->b + off + 4);
			path[plen] = 0;
			for (uint32_t i = 0; i + 4 <= len; i += 4)
				if (!cell_add(o, plen ? path : "/", prop, rd(o->b + off + 12 + i), delta, false))
					return false;
		} else if (tok == T_BEGIN) {
			const char *name = node_name(o, off);
			uint32_t n = slen(name);
			if (plen + 1 + n >= 256)
				return false;
			path[plen] = '/';
			for (uint32_t i = 0; i <= n; i++)
				path[plen + 1 + i] = name[i];
			/* the overlay grows nothing here, so offsets stay valid */
			if (!local_refs(o, off, path, plen + 1 + n, delta))
				return false;
			next = node_end(o, off);
		} else if (tok == T_END || tok == T_DONE) {
			return true;
		}
		off = next;
	}
}

/* __fixups__: label = "path:prop:offset", ... -> the base node's phandle */
static bool fixups(struct dt *base, struct dt *o, uint32_t fx) {
	int32_t syms = dt_path(base, "/__symbols__");
	uint32_t off, next, strs = hdr(o, H_STRINGS);
	token(o, fx, &off);
	for (;;) {
		uint32_t tok = token(o, off, &next);
		if (tok == T_PROP) {
			const char *label = (const char *)o->b + strs + rd(o->b + off + 8);
			uint32_t len = rd(o->b + off + 4), slen_;
			const char *target = (const char *)dt_prop(base, syms, label, &slen_);
			int32_t tn = target ? dt_path(base, target) : -1;
			if (tn < 0) {
				printf("err dtoverlay: label %s not in the base devicetree\n", label);
				return false;
			}
			uint32_t ph = need_phandle(base, tn);
			if (!ph)
				return false;
			/* phandle_of may have grown base: o is a different blob */
			const char *v = (const char *)o->b + off + 12;
			for (uint32_t i = 0; i < len;) {
				char path[256], prop[64];
				uint32_t k = 0, at = 0;
				while (i < len && v[i] != ':' && k < 255)
					path[k++] = v[i++];
				path[k] = 0;
				i++;
				k = 0;
				while (i < len && v[i] != ':' && k < 63)
					prop[k++] = v[i++];
				prop[k] = 0;
				i++;
				while (i < len && v[i] >= '0' && v[i] <= '9')
					at = at * 10 + (uint32_t)(v[i++] - '0');
				i++; /* the NUL */
				if (!cell_add(o, path, prop, at, ph, true))
					return false;
			}
		} else if (tok != T_NOP) {
			return true;
		}
		off = next;
	}
}

/* merge overlay node on (in o) into base node bn, recursively */
static bool merge(struct dt *base, int32_t bn, struct dt *o, uint32_t on) {
	uint32_t off, next, strs = hdr(o, H_STRINGS);
	token(o, on, &off);
	for (;;) {
		uint32_t tok = token(o, off, &next);
		if (tok == T_PROP) {
			const char *name = (const char *)o->b + strs + rd(o->b + off + 8);
			bool ph = seq(name, "phandle") || seq(name, "linux,phandle");
			if (!ph || !phandle_of(base, (uint32_t)bn)) {
				/* the base node's offset stays: changes are at or after it */
				if (!dt_setprop(base, bn, name, o->b + off + 12, rd(o->b + off + 4)))
					return false;
			}
		} else if (tok == T_BEGIN) {
			/* bn's own offset does not move when its children change */
			int32_t c = dt_add_subnode(base, bn, node_name(o, off));
			if (c < 0 || !merge(base, c, o, off))
				return false;
			next = node_end(o, off);
		} else if (tok == T_END || tok == T_DONE) {
			return true;
		}
		off = next;
	}
}

bool dt_apply_overlay(struct dt *base, struct dt *o, const char *name) {
	int32_t root = dt_root(o);
	if (root < 0)
		return false;
	uint32_t delta = max_phandle(base);
	int32_t lf = dt_subnode(o, root, "__local_fixups__");
	char path[256];
	if (!local_phandles(o, delta) || (lf >= 0 && !local_refs(o, (uint32_t)lf, path, 0, delta)))
		return false;
	g_ph_floor = max_phandle(o);
	int32_t fx = dt_subnode(o, root, "__fixups__");
	if (fx >= 0 && !fixups(base, o, (uint32_t)fx))
		return false;
	uint32_t off, next, frags = 0;
	token(o, (uint32_t)root, &off);
	for (;;) {
		uint32_t tok = token(o, off, &next);
		if (tok == T_END || tok == T_DONE)
			break;
		if (tok != T_BEGIN) {
			off = next;
			continue;
		}
		int32_t ov = dt_subnode(o, (int32_t)off, "__overlay__");
		if (ov >= 0) {
			uint32_t len;
			const uint8_t *tv = dt_prop(o, (int32_t)off, "target", &len);
			int32_t tn = -1;
			if (tv && len == 4)
				tn = dt_by_phandle(base, rd(tv));
			else if ((tv = dt_prop(o, (int32_t)off, "target-path", &len)) != 0)
				tn = dt_path(base, (const char *)tv);
			if (tn < 0) {
				printf("err dtoverlay: %s/%s has no target in the base\n", name, node_name(o, off));
				return false;
			}
			if (!merge(base, tn, o, (uint32_t)ov))
				return false;
			frags++;
		}
		off = node_end(o, off);
	}
	printf("dtoverlay: %s, %u fragments\n", name, frags);
	return true;
}

/* ----- what boot64 needs ----- */

/* pad the blob to a multiple of 8 bytes (the kernel reads it as words) */
void dt_finish(struct dt *t) {
	uint32_t u = used(t), a = (u + 7u) & ~7u;
	if (a > t->cap)
		return;
	for (uint32_t i = u; i < a; i++)
		t->b[i] = 0;
	wr(t->b + H_TOTAL, a);
}

/* /memory@0 reg = <base size>, the node made when missing (as start.elf) */
bool dt_set_memory(struct dt *t, uint32_t base, uint32_t size) {
	int32_t m = dt_path(t, "/memory");
	if (m < 0) {
		m = dt_add_subnode(t, dt_root(t), "memory@0");
		if (m < 0 || !dt_setprop_str(t, m, "device_type", "memory"))
			return false;
	}
	uint8_t reg[8];
	wr(reg, base);
	wr(reg + 4, size);
	return dt_setprop(t, m, "reg", reg, 8);
}

static int32_t chosen(struct dt *t) {
	return dt_add_subnode(t, dt_root(t), "chosen");
}

/* /chosen prop = the first line of n bytes at str (tabs as spaces) */
bool dt_set_chosen(struct dt *t, const char *prop, const char *str, uint32_t n) {
	char line[1024];
	uint32_t i = 0;
	for (; i < n && i < sizeof line - 1 && str[i] && str[i] != '\r' && str[i] != '\n'; i++)
		line[i] = str[i] == '\t' ? ' ' : str[i];
	if (i == sizeof line - 1)
		printf("dt: /chosen %s cut at %u bytes\n", prop, i);
	line[i] = 0;
	int32_t c = chosen(t);
	return c >= 0 && dt_setprop(t, c, prop, line, i + 1);
}

bool dt_set_chosen_u32(struct dt *t, const char *prop, uint32_t v) {
	int32_t c = chosen(t);
	return c >= 0 && dt_setprop_u32(t, c, prop, v);
}
