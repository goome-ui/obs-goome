#include "builder.h"
#include "json.h"
#include "overlay.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    BT_WINDOW, BT_ROW, BT_SAME_LINE, BT_SPACE, BT_SEPARATOR, BT_SCROLL, BT_TABS, BT_COLLAPSING, BT_TREE,
    BT_LABEL, BT_HEADING, BT_WRAPPED, BT_BUTTON, BT_CHECKBOX, BT_TOGGLE, BT_RADIO, BT_SLIDER, BT_SLIDER_INT,
    BT_INPUT, BT_COMBO, BT_SELECTABLE, BT_COLOR, BT_PROGRESS, BT_TABLE, BT_PLOT_LINES, BT_PLOT_BARS, BT_IMAGE,
    BT_MENUBAR, BT_MENU, BT_MENU_ITEM, BT_MENU_SEP, BT_POPUP, BT_MODAL, BT_COUNT
};

static const char *const bt_names[BT_COUNT] = {
    "window", "row", "same_line", "space", "separator", "scroll", "tabs", "collapsing", "tree",
    "label", "heading", "wrapped", "button", "checkbox", "toggle", "radio", "slider", "slider_int",
    "input", "combo", "selectable", "color", "progress", "table", "plot_lines", "plot_bars", "image",
    "menubar", "menu", "menu_item", "menu_sep", "popup", "modal",
};

enum { BV_BOOL, BV_INT, BV_FLOAT, BV_COLOR, BV_STR };

typedef struct {
    char name[64];
    int kind;
    int i;
    double d;
    uint32_t c;
    char *s;
    int cap;
    double min, max;
} bv;

typedef struct bn bn;
struct bn {
    int type;
    const js *j, *p;
    const char *id;
    int nslot;
    bn ***sk;
    int *sn;
    double x, y, w, h;
    bv *var;
    int hot;
    char **items;
    int nitems;
    float *vals;
    int nvals;
    goome_bitmap bm;
    int bm_ok;
    const char **cells;
    char **rows;
    int ncols, nrows;
    double *colw;
};

typedef struct {
    int field;
    double v;
} tset;

struct bdoc {
    js *root;
    char dir[1024];
    int app, app_flags;
    char title[128];
    bn **all;
    int nall, capall;
    bn **top;
    int ntop;
    bv **vars;
    int nvars, capvars;
    tset base[96];
    int nbase, base_light;
    char font_names[3][96];
    goome_font *fonts[3];
    int fonts_tried;
    const js **sec;
    int nsec;
    goome_theme *sec_th;
    int sec_built;
    goome_bitmap placeholder;
    int placeholder_ok;
    int skipped;
    goome_ui *u;
    goome *g;
    char ring[4][1024];
    int ringi;
};

typedef struct {
    const char *name;
    size_t off;
    int color;
} tfield;

#define TC(f) {#f, offsetof(goome_theme, f), 1}
#define TN(f) {#f, offsetof(goome_theme, f), 0}

static const tfield tfields[] = {
    TC(bg), TC(panel), TC(border), TC(text), TC(text_dim), TC(accent), TC(accent_text), TC(hover), TC(active), TC(input_bg), TC(shadow),
    TN(radius), TN(pad), TN(gap), TN(border_width), TN(font_size), TN(shadow_blur), TN(anim_ms),
    TC(focus), TC(select), TC(caret), TC(link), TC(badge), TC(toast), TC(tooltip), TC(danger), TC(knob), TC(dim),
    TN(check_size), TN(check_radius), TN(toggle_w), TN(toggle_h), TN(slider_h), TN(slider_track), TN(knob_size),
    TN(scroll_w), TN(scroll_min), TN(item_h), TN(menu_item_h), TN(tab_h), TN(win_title_h), TN(app_title_h), TN(menubar_h), TN(statusbar_h),
    TN(focus_w), TN(focus_gap), TN(sep_w), TN(grip), TN(indent), TN(plot_w), TN(spinner), TN(caret_w),
    TN(win_min_w), TN(win_min_h), TN(pop_gap), TN(menu_gap), TN(tip_pad_x), TN(tip_pad_y), TN(tip_x), TN(tip_y), TN(tip_radius),
    TN(shadow_x), TN(shadow_y), TN(align), TN(heading_size), TN(small_size), TN(ease),
    TN(hover_ms), TN(press_ms), TN(popup_ms), TN(collapse_ms), TN(toast_fade_ms),
};

#define NFIELDS ((int)(sizeof(tfields) / sizeof(tfields[0])))

int ov_theme_field(const char *name, int *is_color)
{
    for (int i = 0; i < NFIELDS; i++)
        if (!strcmp(tfields[i].name, name)) {
            if (is_color) *is_color = tfields[i].color;
            return i;
        }
    return -1;
}

void ov_theme_set(goome_theme *t, int field, double v)
{
    if (field < 0 || field >= NFIELDS) return;
    char *at = (char *)t + tfields[field].off;
    if (tfields[field].color) *(uint32_t *)(void *)at = (uint32_t)(int64_t)v;
    else *(double *)(void *)at = v;
}

static double theme_get(const goome_theme *t, int field)
{
    const char *at = (const char *)t + tfields[field].off;
    if (tfields[field].color) return (double)*(const uint32_t *)(const void *)at;
    return *(const double *)(const void *)at;
}

static double color_of(const js *v, double def)
{
    if (!v) return def;
    if (v->type == JS_NUM) return (double)(uint32_t)(int64_t)v->num;
    if (v->type == JS_STR) {
        const char *s = v->str;
        if (s[0] == '#') {
            size_t n = strlen(s + 1);
            unsigned long x = strtoul(s + 1, NULL, 16);
            if (n == 6) return (double)(0xFF000000u | (uint32_t)x);
            if (n == 8) return (double)(((uint32_t)(x & 255) << 24) | (uint32_t)(x >> 8));
        }
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) return (double)(uint32_t)strtoul(s + 2, NULL, 16);
        return js_num(v, def);
    }
    return def;
}

static double theme_val(int i, const js *v)
{
    if (tfields[i].color) return color_of(v, -1);
    if (!strcmp(tfields[i].name, "ease") && v->type == JS_STR) {
        static const char *const names[] = {"smooth", "linear", "out", "in_out", "back", "custom", "in", "spring", "bounce"};
        const char *s = v->str;
        if (!strncmp(s, "GOOME_EASE_", 11)) s += 11;
        for (int k = 0; k < (int)(sizeof(names) / sizeof(names[0])); k++) {
            const char *a = s, *b = names[k];
            while (*a && *b && (*a | 32) == *b) a++, b++;
            if (!*a && !*b) return k;
        }
    }
    return js_num(v, -1e300);
}

static const char *pstr(const bn *n, const char *k, const char *def)
{
    return js_str(js_get(n->p, k), def);
}

static double pnum(const bn *n, const char *k, double def)
{
    return js_num(js_get(n->p, k), def);
}

static int pbool(const bn *n, const char *k)
{
    return js_bool(js_get(n->p, k), 0);
}

static char *dupstr(const char *s)
{
    size_t n = strlen(s);
    char *o = malloc(n + 1);
    if (o) memcpy(o, s, n + 1);
    return o;
}

static bv *var_find(bdoc *d, const char *name)
{
    if (!name || !name[0]) return NULL;
    for (int i = 0; i < d->nvars; i++)
        if (!strcmp(d->vars[i]->name, name)) return d->vars[i];
    return NULL;
}

static bv *var_new(bdoc *d, const char *name, int kind)
{
    if (d->nvars == d->capvars) {
        int cap = d->capvars ? d->capvars * 2 : 32;
        bv **n = realloc(d->vars, sizeof(bv *) * (size_t)cap);
        if (!n) return NULL;
        d->vars = n, d->capvars = cap;
    }
    bv *v = calloc(1, sizeof(bv));
    if (!v) return NULL;
    snprintf(v->name, sizeof(v->name), "%s", name);
    v->kind = kind;
    if (kind == BV_STR) {
        v->cap = 256;
        v->s = calloc(1, (size_t)v->cap);
        if (!v->s) {
            free(v);
            return NULL;
        }
    }
    d->vars[d->nvars++] = v;
    return v;
}

static void var_text(bv *v, const char *s, int cap)
{
    if (v->kind != BV_STR) return;
    if (cap > v->cap) {
        char *n = realloc(v->s, (size_t)cap);
        if (!n) return;
        v->s = n, v->cap = cap;
    }
    snprintf(v->s, (size_t)v->cap, "%s", s ? s : "");
}

static double var_get(const bv *v)
{
    switch (v->kind) {
    case BV_BOOL:
    case BV_INT: return v->i;
    case BV_FLOAT: return v->d;
    case BV_COLOR: return (double)v->c;
    default: return v->s && v->s[0] ? strtod(v->s, NULL) : 0;
    }
}

static void var_set(bv *v, double x)
{
    if (v->max > v->min && v->kind != BV_COLOR && v->kind != BV_BOOL) x = x < v->min ? v->min : x > v->max ? v->max : x;
    switch (v->kind) {
    case BV_BOOL: v->i = x != 0; break;
    case BV_INT: v->i = (int)x; break;
    case BV_FLOAT: v->d = x; break;
    case BV_COLOR: v->c = (uint32_t)(int64_t)x; break;
    default: {
        char b[64];
        snprintf(b, sizeof(b), "%g", x);
        var_text(v, b, 0);
    }
    }
}

static void var_print(const bv *v, char *out, size_t cap)
{
    double x = var_get(v);
    if (v->kind == BV_STR) snprintf(out, cap, "%s", v->s);
    else if (v->kind == BV_COLOR) snprintf(out, cap, "#%06X", (unsigned)(v->c & 0xFFFFFF));
    else if (v->kind == BV_FLOAT) snprintf(out, cap, "%.2f", x);
    else snprintf(out, cap, "%d", v->i);
}

static const char *interp(bdoc *d, const char *s)
{
    char *out = d->ring[d->ringi++ & 3];
    size_t k = 0, cap = sizeof(d->ring[0]) - 1;
    if (!s) s = "";
    while (*s && k < cap) {
        if (*s == '{') {
            const char *e = strchr(s + 1, '}');
            char name[64];
            size_t n = e ? (size_t)(e - s - 1) : 0;
            if (e && n > 0 && n < sizeof(name)) {
                memcpy(name, s + 1, n);
                name[n] = 0;
                bv *v = var_find(d, name);
                if (v) {
                    char b[512];
                    var_print(v, b, sizeof(b));
                    for (const char *q = b; *q && k < cap; q++) out[k++] = *q;
                    s = e + 1;
                    continue;
                }
            }
        }
        out[k++] = *s++;
    }
    out[k] = 0;
    return out;
}

typedef struct {
    const char *s;
    bdoc *d;
    int bad;
} ex;

static double ex_tern(ex *e);

static void ex_sp(ex *e)
{
    while (*e->s == ' ' || *e->s == '\t' || *e->s == '\n') e->s++;
}

static int ex_is(ex *e, const char *op)
{
    ex_sp(e);
    size_t n = strlen(op);
    if (strncmp(e->s, op, n)) return 0;
    if (n == 1 && (op[0] == '<' || op[0] == '>' || op[0] == '!') && e->s[1] == '=') return 0;
    e->s += n;
    return 1;
}

static int ex_word(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
}

static double ex_prim(ex *e)
{
    ex_sp(e);
    const char *s = e->s;
    if (ex_is(e, "(")) {
        double v = ex_tern(e);
        if (!ex_is(e, ")")) e->bad = 1;
        return v;
    }
    if (ex_is(e, "-")) return -ex_prim(e);
    if (ex_is(e, "!")) return ex_prim(e) == 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        char *end;
        double v = (double)(uint32_t)strtoul(s + 2, &end, 16);
        e->s = end;
        return v;
    }
    if (s[0] == '#') {
        size_t n = 0;
        while (ex_word(s[1 + n]) && s[1 + n] != '.') n++;
        char b[16];
        if (n != 6 && n != 8) return e->bad = 1, 0;
        memcpy(b, s + 1, n);
        b[n] = 0;
        unsigned long x = strtoul(b, NULL, 16);
        e->s = s + 1 + n;
        return n == 6 ? (double)(0xFF000000u | (uint32_t)x) : (double)(((uint32_t)(x & 255) << 24) | (uint32_t)(x >> 8));
    }
    if ((s[0] >= '0' && s[0] <= '9') || s[0] == '.') {
        char *end;
        double v = strtod(s, &end);
        e->s = end;
        return v;
    }
    if (ex_word(s[0])) {
        char name[64];
        size_t n = 0;
        while (ex_word(s[n]) && n < sizeof(name) - 1) name[n] = s[n], n++;
        name[n] = 0;
        e->s = s + n;
        if (!strcmp(name, "true")) return 1;
        if (!strcmp(name, "false")) return 0;
        if (!strcmp(name, "min") || !strcmp(name, "max") || !strcmp(name, "abs")) {
            if (!ex_is(e, "(")) return e->bad = 1, 0;
            double a = ex_tern(e), b = 0;
            if (name[1] != 'b') {
                if (!ex_is(e, ",")) return e->bad = 1, 0;
                b = ex_tern(e);
            }
            if (!ex_is(e, ")")) e->bad = 1;
            if (name[1] == 'b') return a < 0 ? -a : a;
            return name[1] == 'i' ? (a < b ? a : b) : (a > b ? a : b);
        }
        if (!strncmp(name, "th.", 3) && e->d->u) {
            int f = ov_theme_field(name + 3, NULL);
            if (f >= 0) return theme_get(goome_ui_theme(e->d->u), f);
            return e->bad = 1, 0;
        }
        bv *v = var_find(e->d, name);
        if (!v) return e->bad = 1, 0;
        return var_get(v);
    }
    e->bad = 1;
    return 0;
}

static double ex_mul(ex *e)
{
    double a = ex_prim(e);
    for (;;) {
        if (ex_is(e, "*")) a *= ex_prim(e);
        else if (ex_is(e, "/")) {
            double b = ex_prim(e);
            a = b == 0 ? 0 : a / b;
        } else return a;
    }
}

static double ex_add(ex *e)
{
    double a = ex_mul(e);
    for (;;) {
        if (ex_is(e, "+")) a += ex_mul(e);
        else if (ex_is(e, "-")) a -= ex_mul(e);
        else return a;
    }
}

static double ex_rel(ex *e)
{
    double a = ex_add(e);
    for (;;) {
        if (ex_is(e, "<=")) a = a <= ex_add(e);
        else if (ex_is(e, ">=")) a = a >= ex_add(e);
        else if (ex_is(e, "<")) a = a < ex_add(e);
        else if (ex_is(e, ">")) a = a > ex_add(e);
        else return a;
    }
}

static double ex_eq(ex *e)
{
    double a = ex_rel(e);
    for (;;) {
        if (ex_is(e, "==")) a = a == ex_rel(e);
        else if (ex_is(e, "!=")) a = a != ex_rel(e);
        else return a;
    }
}

static double ex_and(ex *e)
{
    double a = ex_eq(e);
    while (ex_is(e, "&&")) {
        double b = ex_eq(e);
        a = a != 0 && b != 0;
    }
    return a;
}

static double ex_or(ex *e)
{
    double a = ex_and(e);
    while (ex_is(e, "||")) {
        double b = ex_and(e);
        a = a != 0 || b != 0;
    }
    return a;
}

static double ex_tern(ex *e)
{
    double c = ex_or(e);
    if (!ex_is(e, "?")) return c;
    double a = ex_tern(e);
    if (!ex_is(e, ":")) return e->bad = 1, 0;
    double b = ex_tern(e);
    return c != 0 ? a : b;
}

static int calc(bdoc *d, const js *v, double *out)
{
    if (!v) return 0;
    if (v->type == JS_NUM || v->type == JS_BOOL) return *out = v->num, 1;
    if (v->type != JS_STR) return 0;
    ex e = {v->str, d, 0};
    ex_sp(&e);
    if (!*e.s) return 0;
    double r = ex_tern(&e);
    ex_sp(&e);
    if (e.bad || *e.s) return 0;
    *out = r;
    return 1;
}

static int lines(const char *s, char ***out)
{
    int n = 0;
    *out = NULL;
    if (!s) return 0;
    for (const char *p = s; *p;) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        while (len && (p[0] == ' ' || p[0] == '\t' || p[0] == '\r')) p++, len--;
        while (len && (p[len - 1] == ' ' || p[len - 1] == '\t' || p[len - 1] == '\r')) len--;
        if (len) {
            char **g = realloc(*out, sizeof(char *) * (size_t)(n + 1));
            if (!g) break;
            *out = g;
            g[n] = malloc(len + 1);
            if (!g[n]) break;
            memcpy(g[n], p, len);
            g[n++][len] = 0;
        }
        if (!e) break;
        p = e + 1;
    }
    return n;
}

static int b64v(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

static unsigned char *unb64(const char *s, size_t *len)
{
    size_t n = strlen(s), k = 0;
    unsigned char *out = malloc(n / 4 * 3 + 4);
    if (!out) return NULL;
    unsigned acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; i++) {
        int v = b64v(s[i]);
        if (v < 0) continue;
        acc = (acc << 6) | (unsigned)v, bits += 6;
        if (bits >= 8) bits -= 8, out[k++] = (unsigned char)(acc >> bits);
    }
    *len = k;
    return out;
}

static void load_image(bdoc *d, bn *n)
{
    const char *img = pstr(n, "img", ""), *file = pstr(n, "file", "");
    const char *comma = strstr(img, "base64,");
    if (comma) {
        size_t len;
        unsigned char *raw = unb64(comma + 7, &len);
        if (raw) {
            n->bm_ok = goome_bitmap_load_mem(raw, len, &n->bm) == GOOME_OK;
            free(raw);
        }
    }
    if (!n->bm_ok && file[0]) {
        char path[2048];
        int abs = file[0] == '/' || file[0] == '\\' || (file[0] && file[1] == ':');
        snprintf(path, sizeof(path), "%s%s", abs ? "" : d->dir, file);
        n->bm_ok = goome_bitmap_load(path, &n->bm) == GOOME_OK;
    }
}

static void load_table(bn *n)
{
    char **cols, **rows;
    int nc = lines(pstr(n, "cols", ""), &cols), nr = lines(pstr(n, "rows", ""), &rows);
    if (!nc) {
        free(cols);
        cols = malloc(sizeof(char *));
        if (!cols) return;
        cols[0] = dupstr("Column"), nc = 1;
    }
    n->ncols = nc, n->nrows = nr;
    n->items = cols, n->nitems = nc;
    n->colw = calloc((size_t)nc, sizeof(double));
    n->cells = calloc((size_t)nc * (size_t)(nr ? nr : 1), sizeof(char *));
    if (!n->colw || !n->cells) {
        n->ncols = n->nrows = 0;
        for (int i = 0; i < nr; i++) free(rows[i]);
        free(rows);
        return;
    }
    for (int c = 0; c < nc; c++) {
        char *colon = strrchr(cols[c], ':');
        if (colon && colon[1] >= '0' && colon[1] <= '9') {
            n->colw[c] = strtod(colon + 1, NULL);
            *colon = 0;
        }
    }
    for (int r = 0; r < nr; r++) {
        char *p = rows[r];
        for (int c = 0; c < nc; c++) {
            char *e = p ? strchr(p, ',') : NULL;
            if (e) *e = 0;
            while (p && *p == ' ') p++;
            n->cells[(size_t)r * (size_t)nc + (size_t)c] = p ? p : "";
            p = e ? e + 1 : NULL;
        }
    }
    n->rows = rows;
}

static void load_plot(bn *n)
{
    const char *s = pstr(n, "values", "");
    int cap = 0;
    while (*s) {
        while (*s == ' ' || *s == ',' || *s == '\n' || *s == '\t') s++;
        if (!*s) break;
        char *end;
        double v = strtod(s, &end);
        if (end == s) {
            s++;
            continue;
        }
        if (n->nvals == cap) {
            cap = cap ? cap * 2 : 16;
            float *g = realloc(n->vals, sizeof(float) * (size_t)cap);
            if (!g) return;
            n->vals = g;
        }
        n->vals[n->nvals++] = (float)v;
        s = end;
    }
}

static int fmt_ok(const char *f)
{
    int seen = 0;
    for (; *f; f++) {
        if (*f != '%') continue;
        if (*++f == '%') continue;
        if (seen++) return 0;
        while (*f && strchr("-+ 0#", *f)) f++;
        for (int k = 0; *f >= '0' && *f <= '9'; f++)
            if (++k > 3) return 0;
        if (*f == '.') f++;
        for (int k = 0; *f >= '0' && *f <= '9'; f++)
            if (++k > 3) return 0;
        if (!*f || !strchr("fFeEgG", *f)) return 0;
    }
    return 1;
}

static int own_kind(int type)
{
    switch (type) {
    case BT_CHECKBOX: case BT_TOGGLE: case BT_COLLAPSING: case BT_TREE: case BT_MODAL: case BT_WINDOW: case BT_MENU_ITEM: return BV_BOOL;
    case BT_SLIDER: return BV_FLOAT;
    case BT_SLIDER_INT: case BT_COMBO: case BT_RADIO: case BT_SELECTABLE: case BT_TABS: return BV_INT;
    case BT_COLOR: return BV_COLOR;
    case BT_INPUT: return BV_STR;
    case BT_PROGRESS: return -2;
    default: return -1;
    }
}

static int kind_fits(int type, int kind)
{
    int want = own_kind(type);
    if (want == -2) return kind == BV_FLOAT || kind == BV_INT;
    if (type == BT_SLIDER) return kind == BV_FLOAT || kind == BV_INT;
    return want == kind;
}

static void node_var(bdoc *d, bn *n)
{
    int kind = own_kind(n->type);
    if (kind == -1) return;
    bv *b = var_find(d, pstr(n, "bind", ""));
    if (b && kind_fits(n->type, b->kind)) {
        n->var = b;
        return;
    }
    if (kind == -2) return;
    const char *name = pstr(n, "var", "");
    char own[64];
    if (!name[0] || var_find(d, name)) snprintf(own, sizeof(own), "#%s", n->id), name = own;
    bv *v = var_new(d, name, kind);
    if (!v) return;
    n->var = v;
    switch (n->type) {
    case BT_CHECKBOX: case BT_TOGGLE: case BT_MENU_ITEM: v->i = pbool(n, "on"); break;
    case BT_COLLAPSING: case BT_TREE: v->i = pbool(n, "open"); break;
    case BT_WINDOW: v->i = 1; break;
    case BT_SLIDER: v->d = pnum(n, "value", 0); break;
    case BT_SLIDER_INT: v->i = (int)pnum(n, "value", 0); break;
    case BT_COMBO: case BT_RADIO: case BT_SELECTABLE: case BT_TABS: v->i = (int)pnum(n, "sel", 0); break;
    case BT_COLOR: v->c = (uint32_t)color_of(js_get(n->p, "color"), 0xFF4FA3FF); break;
    case BT_INPUT: {
        int cap = (int)pnum(n, "cap", 64) + 1;
        var_text(v, pstr(n, "text", ""), cap < 256 ? 256 : cap);
        break;
    }
    }
}

static bn *node_load(bdoc *d, const js *j);

static void node_kids(bdoc *d, bn *n)
{
    const js *kids = js_get(n->j, "kids");
    int ns = kids && kids->type == JS_ARR ? kids->n : 0;
    if (n->type == BT_TABS && n->nitems > ns) ns = n->nitems;
    if (!ns) return;
    n->sk = calloc((size_t)ns, sizeof(bn **));
    n->sn = calloc((size_t)ns, sizeof(int));
    if (!n->sk || !n->sn) return;
    n->nslot = ns;
    for (int s = 0; s < ns; s++) {
        const js *list = js_at(kids, s);
        if (!list || list->type != JS_ARR || !list->n) continue;
        n->sk[s] = calloc((size_t)list->n, sizeof(bn *));
        if (!n->sk[s]) continue;
        for (int i = 0; i < list->n; i++) {
            bn *k = node_load(d, &list->kids[i]);
            if (k) n->sk[s][n->sn[s]++] = k;
        }
    }
}

static bn *node_load(bdoc *d, const js *j)
{
    const char *t = js_str(js_get(j, "type"), "");
    int type = -1;
    for (int i = 0; i < BT_COUNT; i++)
        if (!strcmp(bt_names[i], t)) type = i;
    if (!strcmp(t, "toast")) type = BT_BUTTON;
    if (type < 0) {
        d->skipped++;
        return NULL;
    }
    bn *n = calloc(1, sizeof(bn));
    if (!n) return NULL;
    if (d->nall == d->capall) {
        int cap = d->capall ? d->capall * 2 : 64;
        bn **g = realloc(d->all, sizeof(bn *) * (size_t)cap);
        if (!g) {
            free(n);
            return NULL;
        }
        d->all = g, d->capall = cap;
    }
    d->all[d->nall++] = n;
    n->type = type, n->j = j, n->p = js_get(j, "p");
    n->id = js_str(js_get(j, "id"), "");
    if (type == BT_WINDOW) {
        n->x = pnum(n, "x", 40), n->y = pnum(n, "y", 40), n->w = pnum(n, "w", 340), n->h = pnum(n, "h", 0);
    }
    if (type == BT_TABS || type == BT_RADIO || type == BT_COMBO || type == BT_SELECTABLE) {
        n->nitems = lines(pstr(n, "items", ""), &n->items);
        if (type == BT_TABS && !n->nitems) {
            free(n->items);
            n->items = malloc(sizeof(char *));
            if (n->items) n->items[0] = dupstr("Tab"), n->nitems = 1;
        }
    }
    if (type == BT_PLOT_LINES || type == BT_PLOT_BARS) load_plot(n);
    if (type == BT_TABLE) load_table(n);
    if (type == BT_IMAGE) load_image(d, n);
    node_var(d, n);
    node_kids(d, n);
    return n;
}

static void load_vars(bdoc *d, const js *vars)
{
    for (int i = 0; vars && vars->type == JS_ARR && i < vars->n; i++) {
        const js *v = &vars->kids[i];
        const char *name = js_str(js_get(v, "name"), ""), *type = js_str(js_get(v, "type"), "");
        if (!name[0] || var_find(d, name)) continue;
        int kind = !strcmp(type, "bool") ? BV_BOOL : !strcmp(type, "int") ? BV_INT : !strcmp(type, "float") ? BV_FLOAT
            : !strcmp(type, "color") ? BV_COLOR : (!strcmp(type, "string") || !strcmp(type, "list")) ? BV_STR : -1;
        if (kind < 0) continue;
        bv *b = var_new(d, name, kind);
        if (!b) continue;
        const js *def = js_get(v, "def");
        b->min = js_num(js_get(v, "min"), 0), b->max = js_num(js_get(v, "max"), 0);
        if (kind == BV_STR) var_text(b, js_str(def, ""), 0);
        else if (kind == BV_COLOR) b->c = (uint32_t)color_of(def, 0xFF6FDC5F);
        else if (kind == BV_FLOAT) b->d = js_num(def, 0);
        else b->i = (int)js_num(def, 0);
    }
}

static void load_theme(bdoc *d, const js *t)
{
    if (!t || t->type != JS_OBJ) return;
    d->base_light = !strcmp(js_str(js_get(t, "base"), "dark"), "light");
    for (int i = 0; i < NFIELDS && d->nbase < (int)(sizeof(d->base) / sizeof(d->base[0])); i++) {
        const js *v = js_get(t, tfields[i].name);
        if (!v) continue;
        double x = theme_val(i, v);
        if ((tfields[i].color && x < 0) || x == -1e300) continue;
        d->base[d->nbase].field = i, d->base[d->nbase++].v = x;
    }
    static const char *const fk[3] = {"font_heading", "font_mono", "font_small"};
    for (int i = 0; i < 3; i++) snprintf(d->font_names[i], sizeof(d->font_names[i]), "%s", js_str(js_get(t, fk[i]), ""));
}

static void apply_theme_obj(bdoc *d, goome_theme *t, const js *o, int light)
{
    goome_font *keep = t->font, *h = t->font_heading, *m = t->font_mono, *s = t->font_small;
    if (light) goome_ui_theme_light(t);
    else goome_ui_theme_dark(t);
    t->font = keep, t->font_heading = h, t->font_mono = m, t->font_small = s;
    if (o == NULL) {
        for (int i = 0; i < d->nbase; i++) ov_theme_set(t, d->base[i].field, d->base[i].v);
        return;
    }
    for (int i = 0; i < NFIELDS; i++) {
        const js *v = js_get(o, tfields[i].name);
        if (!v) continue;
        double x = theme_val(i, v);
        if ((tfields[i].color && x < 0) || x == -1e300) continue;
        ov_theme_set(t, i, x);
    }
}

void bdoc_theme(bdoc *d, goome_theme *t)
{
    if (!d || !t) return;
    apply_theme_obj(d, t, NULL, d->base_light);
    if (!d->fonts_tried) {
        d->fonts_tried = 1;
        for (int i = 0; i < 3; i++)
            if (d->font_names[i][0]) d->fonts[i] = goome_font_system(d->font_names[i]);
    }
    if (d->fonts[0]) t->font_heading = d->fonts[0];
    if (d->fonts[1]) t->font_mono = d->fonts[1];
    if (d->fonts[2]) t->font_small = d->fonts[2];
}

static int ends_with(const char *s, const char *tail)
{
    size_t a = strlen(s), b = strlen(tail);
    return a >= b && !strcmp(s + a - b, tail);
}

bdoc *bdoc_parse(const char *text, size_t len, const char *dir, char *err, int errcap)
{
    bdoc *d = calloc(1, sizeof(bdoc));
    if (!d) return NULL;
    d->root = js_parse(text, len, err, errcap);
    if (!d->root) {
        free(d);
        return NULL;
    }
    const js *r = d->root, *root = js_get(r, "root");
    if (r->type != JS_OBJ || !root || root->type != JS_ARR) {
        if (err && errcap > 0) snprintf(err, (size_t)errcap, "thats not a goome builder project, it has no root list");
        bdoc_free(d);
        return NULL;
    }
    snprintf(d->dir, sizeof(d->dir), "%s", dir ? dir : "");
    if (d->dir[0] && !ends_with(d->dir, "/") && !ends_with(d->dir, "\\")) strncat(d->dir, "/", sizeof(d->dir) - strlen(d->dir) - 1);
    d->app = !strcmp(js_str(js_get(r, "mode"), "overlay"), "app");
    snprintf(d->title, sizeof(d->title), "%s", js_str(js_get(r, "title"), "goome ui"));
    const js *app = js_get(r, "app");
    if (js_bool(js_get(app, "titlebar"), 0)) d->app_flags |= GOOME_APP_TITLEBAR;
    if (js_bool(js_get(app, "menubar"), 0)) d->app_flags |= GOOME_APP_MENUBAR;
    if (js_bool(js_get(app, "statusbar"), 0)) d->app_flags |= GOOME_APP_STATUSBAR;
    if (js_bool(js_get(app, "nopad"), 0)) d->app_flags |= GOOME_APP_NO_PAD;
    load_vars(d, js_get(r, "vars"));
    load_theme(d, js_get(r, "theme"));
    const js *themes = js_get(r, "themes");
    if (themes && themes->type == JS_ARR && themes->n) {
        d->sec = calloc((size_t)themes->n, sizeof(js *));
        d->sec_th = calloc((size_t)themes->n, sizeof(goome_theme));
        if (d->sec && d->sec_th)
            for (int i = 0; i < themes->n; i++) d->sec[d->nsec++] = &themes->kids[i];
    }
    d->top = calloc((size_t)(root->n ? root->n : 1), sizeof(bn *));
    if (!d->top) {
        bdoc_free(d);
        return NULL;
    }
    for (int i = 0; i < root->n; i++) {
        bn *n = node_load(d, &root->kids[i]);
        if (n) d->top[d->ntop++] = n;
    }
    uint32_t px[64 * 64];
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            int k = ((x / 8) + (y / 8)) & 1;
            px[y * 64 + x] = k ? 0xFF3A3F48u : 0xFF2C3038u;
        }
    d->placeholder_ok = goome_bitmap_from_argb(px, 64, 64, &d->placeholder) == GOOME_OK;
    return d;
}

bdoc *bdoc_load(const char *path, char *err, int errcap)
{
    FILE *f = ov_fopen(path, "rb");
    if (!f) {
        if (err && errcap > 0) snprintf(err, (size_t)errcap, "cant open %s", path);
        return NULL;
    }
    char *buf = NULL;
    size_t len = 0, cap = 0;
    for (;;) {
        if (len + 65536 > cap) {
            cap = cap ? cap * 2 : 131072;
            if (cap > ((size_t)64 << 20)) break;
            char *g = realloc(buf, cap);
            if (!g) break;
            buf = g;
        }
        size_t got = fread(buf + len, 1, cap - len, f);
        len += got;
        if (!got) break;
    }
    fclose(f);
    if (!buf) return NULL;
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/'), *bs = strrchr(dir, '\\');
    if (bs > slash) slash = bs;
    if (slash) slash[1] = 0;
    else dir[0] = 0;
    bdoc *d = bdoc_parse(buf, len, dir, err, errcap);
    free(buf);
    return d;
}

static void node_free(bn *n)
{
    for (int s = 0; s < n->nslot; s++) free(n->sk[s]);
    free(n->sk);
    free(n->sn);
    for (int i = 0; i < n->nitems; i++) free(n->items[i]);
    free(n->items);
    for (int i = 0; n->rows && i < n->nrows; i++) free(n->rows[i]);
    free(n->rows);
    free(n->vals);
    free((void *)n->cells);
    free(n->colw);
    if (n->bm_ok) goome_bitmap_free(&n->bm);
    free(n);
}

void bdoc_free(bdoc *d)
{
    if (!d) return;
    for (int i = 0; i < d->nall; i++) node_free(d->all[i]);
    free(d->all);
    free(d->top);
    for (int i = 0; i < d->nvars; i++) {
        free(d->vars[i]->s);
        free(d->vars[i]);
    }
    free(d->vars);
    for (int i = 0; i < 3; i++) goome_font_free(d->fonts[i]);
    free(d->sec);
    free(d->sec_th);
    if (d->placeholder_ok) goome_bitmap_free(&d->placeholder);
    js_free(d->root);
    free(d->root);
    free(d);
}

int bdoc_count(const bdoc *d)
{
    return d ? d->nall : 0;
}

int bdoc_skipped(const bdoc *d)
{
    return d ? d->skipped : 0;
}

static bn *by_id(bdoc *d, const char *id)
{
    if (!id || !id[0]) return NULL;
    for (int i = 0; i < d->nall; i++)
        if (!strcmp(d->all[i]->id, id)) return d->all[i];
    return NULL;
}

static const js *events(const bn *n, const char *what)
{
    const js *a = js_get(js_get(n->j, "on"), what);
    return a && a->type == JS_ARR && a->n ? a : NULL;
}

static void run_actions(bdoc *d, const js *list)
{
    for (int i = 0; list && i < list->n; i++) {
        const js *a = &list->kids[i];
        const char *what = js_str(js_get(a, "do"), "");
        bv *v = var_find(d, js_str(js_get(a, "var"), ""));
        double x;
        if (!strcmp(what, "set") && v) {
            if (v->kind == BV_STR) var_text(v, interp(d, js_str(js_get(a, "value"), "")), 0);
            else if (calc(d, js_get(a, "value"), &x)) var_set(v, x);
        } else if (!strcmp(what, "toggle") && v) var_set(v, var_get(v) == 0);
        else if ((!strcmp(what, "add") || !strcmp(what, "sub")) && v && v->kind != BV_STR) {
            const js *val = js_get(a, "value");
            if (!val || (val->type == JS_STR && !val->str[0]) || !calc(d, val, &x)) x = 1;
            var_set(v, var_get(v) + (what[0] == 'a' ? x : -x));
        } else if (!strcmp(what, "open") || !strcmp(what, "close")) {
            bn *t = by_id(d, js_str(js_get(a, "target"), ""));
            int on = what[0] == 'o';
            if (!t) continue;
            if (t->type == BT_POPUP) {
                if (on) goome_ui_popup_open(d->u, pstr(t, "label", "popup"));
                else goome_ui_popup_close(d->u);
            } else if (t->var && (t->type == BT_WINDOW || t->type == BT_MODAL)) t->var->i = on;
        } else if (!strcmp(what, "toast")) {
            int ms = (int)js_num(js_get(a, "ms"), 2500);
            goome_ui_toast(d->u, interp(d, js_str(js_get(a, "text"), "")), ms > 0 ? ms : 2500);
        } else if (!strcmp(what, "tab")) {
            bn *t = by_id(d, js_str(js_get(a, "target"), ""));
            if (t && t->type == BT_TABS && t->var) t->var->i = (int)js_num(js_get(a, "tab"), 0);
        } else if (!strcmp(what, "copy") && d->g) goome_clipboard_set(d->g, interp(d, js_str(js_get(a, "text"), "")));
    }
}

static void fire(bdoc *d, const bn *n, const char *what)
{
    run_actions(d, events(n, what));
}

static void changed(bdoc *d, const bn *n)
{
    fire(d, n, "change");
    if (n->var && var_get(n->var) != 0) fire(d, n, "on");
    else fire(d, n, "off");
}

static void hover(bdoc *d, bn *n)
{
    int h = goome_ui_hovered(d->u);
    if (h && !n->hot) fire(d, n, "hover");
    n->hot = h;
}

static void draw_list(bdoc *d, bn **list, int n, int root);

static int push_style(bdoc *d, const js *st)
{
    if (!st || st->type != JS_OBJ) return 0;
    goome_ui *u = d->u;
    int k = 0;
    const char *th = js_str(js_get(st, "theme"), "");
    if (th[0]) {
        static const char *const presets[] = {"dark", "light", "midnight", "paper", "mint", "contrast", "rose"};
        int done = 0;
        for (int i = 0; i < d->nsec; i++)
            if (!strcmp(js_str(js_get(d->sec[i], "id"), ""), th)) {
                if (!(d->sec_built & (1 << (i & 31)))) {
                    d->sec_th[i] = *goome_ui_theme(u);
                    apply_theme_obj(d, &d->sec_th[i], d->sec[i], !strcmp(js_str(js_get(d->sec[i], "base"), "dark"), "light"));
                    d->sec_built |= 1 << (i & 31);
                }
                goome_ui_push_theme(u, &d->sec_th[i]), k++, done = 1;
                break;
            }
        for (int i = 0; !done && i < 7; i++)
            if (!strcmp(presets[i], th)) {
                goome_theme t = *goome_ui_theme(u);
                goome_ui_theme_preset(&t, i);
                t.font = goome_ui_theme(u)->font;
                goome_ui_push_theme(u, &t), k++;
                break;
            }
    }
    const js *c = js_get(st, "c"), *nums = js_get(st, "n"), *radii = js_get(st, "radii");
    for (int i = 0; c && c->type == JS_OBJ && i < c->n; i++) {
        int col, f = ov_theme_field(c->kids[i].key, &col);
        if (f >= 0 && col && c->kids[i].type != JS_NULL) goome_ui_push_color(u, f, (uint32_t)color_of(&c->kids[i], 0)), k++;
    }
    for (int i = 0; nums && nums->type == JS_OBJ && i < nums->n; i++) {
        int col, f = ov_theme_field(nums->kids[i].key, &col);
        if (f < 0 || col) continue;
        double x = theme_val(f, &nums->kids[i]);
        if (x != -1e300) goome_ui_push_num(u, f, x), k++;
    }
    if (radii && radii->type == JS_ARR && radii->n == 4) {
        goome_radii r = {js_num(&radii->kids[0], 0), js_num(&radii->kids[1], 0), js_num(&radii->kids[2], 0), js_num(&radii->kids[3], 0)};
        goome_ui_push_radii(u, r), k++;
    }
    return k;
}

static void draw_slot(bdoc *d, bn *n, int s)
{
    if (s < 0 || s >= n->nslot) return;
    char key[16];
    snprintf(key, sizeof(key), "%d", s);
    int k = push_style(d, js_get(js_get(n->j, "slotStyle"), key));
    draw_list(d, n->sk[s], n->sn[s], 0);
    if (k) goome_ui_pop(d->u, k);
}

static int win_flags(const bn *n)
{
    int f = 0;
    if (pbool(n, "move")) f |= GOOME_WIN_MOVE;
    if (pbool(n, "resize")) f |= GOOME_WIN_RESIZE;
    if (pbool(n, "collapse")) f |= GOOME_WIN_COLLAPSE;
    if (pbool(n, "notitle")) f |= GOOME_WIN_NO_TITLE;
    if (pbool(n, "close")) f |= GOOME_WIN_CLOSE;
    return f;
}

static void draw_node(bdoc *d, bn *n, int root)
{
    goome_ui *u = d->u;
    const char *label = interp(d, pstr(n, "label", ""));
    int leaf = n->type >= BT_LABEL && n->type <= BT_IMAGE, ch = 0;
    if (leaf && pnum(n, "width", 0) > 0) goome_ui_width(u, pnum(n, "width", 0));
    const js *abs = js_get(n->j, "abs");
    if (abs && !root && n->type != BT_WINDOW) {
        goome_ui_pos(u, js_num(js_get(abs, "x"), 0), js_num(js_get(abs, "y"), 0));
        double w = js_num(js_get(abs, "w"), 0), h = js_num(js_get(abs, "h"), 0);
        if (w > 0 || h > 0) goome_ui_size(u, w > 0 ? w : 0, h > 0 ? h : 0);
    }
    bv *v = n->var;
    switch (n->type) {
    case BT_WINDOW:
        if (goome_ui_window(u, label[0] ? label : "window", &n->x, &n->y, &n->w, &n->h, win_flags(n), v ? &v->i : NULL)) draw_slot(d, n, 0);
        goome_ui_window_end(u);
        return;
    case BT_ROW: {
        int k = (int)pnum(n, "cols", 0);
        if (k <= 0) k = n->nslot ? n->sn[0] : 0;
        if (k >= 2) goome_ui_row(u, k);
        draw_slot(d, n, 0);
        return;
    }
    case BT_SAME_LINE: goome_ui_same_line(u); return;
    case BT_SPACE: goome_ui_space(u, pnum(n, "px", 12)); return;
    case BT_SEPARATOR: goome_ui_separator(u); return;
    case BT_SCROLL:
        if (goome_ui_scroll_begin(u, n->id, pnum(n, "h", 150))) draw_slot(d, n, 0);
        goome_ui_scroll_end(u);
        return;
    case BT_TABS:
        if (!v) return;
        if (goome_ui_tabs(u, n->id, (const char *const *)n->items, n->nitems, &v->i)) changed(d, n);
        draw_slot(d, n, v->i);
        return;
    case BT_COLLAPSING: {
        if (!v) return;
        int was = v->i, open = goome_ui_collapsing(u, label, &v->i);
        if (was != v->i) changed(d, n);
        if (open) draw_slot(d, n, 0);
        return;
    }
    case BT_TREE:
        if (!v) return;
        if (goome_ui_tree(u, label, &v->i)) {
            draw_slot(d, n, 0);
            goome_ui_tree_end(u);
        }
        return;
    case BT_LABEL: goome_ui_label(u, "%s", label); hover(d, n); break;
    case BT_HEADING: goome_ui_heading(u, label); hover(d, n); break;
    case BT_WRAPPED: goome_ui_wrapped(u, label); break;
    case BT_BUTTON:
        if (goome_ui_button(u, label)) {
            if (events(n, "click")) fire(d, n, "click");
            else if (!strcmp(pstr(n, "action", ""), "toast")) goome_ui_toast(u, interp(d, pstr(n, "toast", "")), (int)pnum(n, "ms", 2500));
            else if (!strcmp(pstr(n, "action", ""), "close")) goome_ui_popup_close(u);
        }
        hover(d, n);
        break;
    case BT_CHECKBOX:
    case BT_TOGGLE:
        if (!v) return;
        ch = n->type == BT_CHECKBOX ? goome_ui_checkbox(u, label, &v->i) : goome_ui_toggle(u, label, &v->i);
        if (ch) changed(d, n);
        hover(d, n);
        break;
    case BT_RADIO:
        if (!v) return;
        for (int i = 0; i < n->nitems; i++) {
            if (i && pbool(n, "inline")) goome_ui_same_line(u);
            ch |= goome_ui_radio(u, n->items[i], &v->i, i);
        }
        if (ch) changed(d, n);
        break;
    case BT_SLIDER:
        if (!v) return;
        if (v->kind == BV_INT) ch = goome_ui_slider_int(u, label, &v->i, (int)pnum(n, "min", 0), (int)pnum(n, "max", 100));
        else {
            const char *fmt = pstr(n, "fmt", "");
            ch = goome_ui_slider(u, label, &v->d, pnum(n, "min", 0), pnum(n, "max", 1), fmt[0] && fmt_ok(fmt) ? fmt : NULL);
        }
        if (ch) changed(d, n);
        hover(d, n);
        break;
    case BT_SLIDER_INT:
        if (!v) return;
        if (goome_ui_slider_int(u, label, &v->i, (int)pnum(n, "min", 0), (int)pnum(n, "max", 100))) changed(d, n);
        hover(d, n);
        break;
    case BT_INPUT: {
        if (!v || v->kind != BV_STR) return;
        int fl = (pbool(n, "password") ? GOOME_INPUT_PASSWORD : 0) | (pbool(n, "readonly") ? GOOME_INPUT_READONLY : 0) | (pbool(n, "number") ? GOOME_INPUT_NUMBER : 0)
            | (pbool(n, "enter") ? GOOME_INPUT_ENTER_RETURNS : 0) | (pbool(n, "noselect") ? GOOME_INPUT_NO_SELECT_ON_FOCUS : 0);
        const char *hint = pstr(n, "hint", "");
        if (goome_ui_input_ex(u, label, v->s, v->cap, fl, hint[0] ? hint : NULL)) changed(d, n);
        hover(d, n);
        break;
    }
    case BT_COMBO:
        if (!v || !n->nitems) return;
        if (goome_ui_combo(u, label, &v->i, (const char *const *)n->items, n->nitems)) changed(d, n);
        hover(d, n);
        break;
    case BT_SELECTABLE:
        if (!v) return;
        for (int i = 0; i < n->nitems; i++)
            if (goome_ui_selectable(u, n->items[i], v->i == i) && v->i != i) v->i = i, ch = 1;
        if (ch) changed(d, n);
        break;
    case BT_COLOR:
        if (!v) return;
        if (goome_ui_color(u, label, &v->c)) changed(d, n);
        hover(d, n);
        break;
    case BT_PROGRESS: {
        double f = pnum(n, "value", 0);
        if (v) {
            f = var_get(v);
            if (v->max > v->min && !(v->min == 0 && v->max == 1)) f = (f - v->min) / (v->max - v->min);
        }
        goome_ui_progress(u, f < 0 ? 0 : f > 1 ? 1 : f, label[0] ? label : NULL);
        hover(d, n);
        break;
    }
    case BT_TABLE: {
        if (!n->ncols) return;
        int fl = (pbool(n, "sortable") ? GOOME_TABLE_SORTABLE : 0) | (pbool(n, "resizable") ? GOOME_TABLE_RESIZABLE : 0)
            | (pbool(n, "stripes") ? GOOME_TABLE_STRIPES : 0) | (pbool(n, "borders") ? GOOME_TABLE_BORDERS : 0);
        if (goome_ui_table_begin(u, n->id, n->ncols, pnum(n, "h", 0), fl)) {
            for (int c = 0; c < n->ncols; c++) goome_ui_table_column(u, n->items[c], n->colw[c]);
            for (int r = 0; r < n->nrows; r++) {
                goome_ui_table_row(u);
                for (int c = 0; c < n->ncols; c++) {
                    if (c) goome_ui_table_next(u);
                    goome_ui_label(u, "%s", n->cells[(size_t)r * (size_t)n->ncols + (size_t)c]);
                }
            }
            goome_ui_table_end(u);
        }
        break;
    }
    case BT_PLOT_LINES:
    case BT_PLOT_BARS: {
        static const float none = 0;
        const float *vals = n->nvals ? n->vals : &none;
        int k = n->nvals ? n->nvals : 1;
        if (n->type == BT_PLOT_LINES) goome_ui_plot_lines(u, label, vals, k, pnum(n, "min", 0), pnum(n, "max", 0), pnum(n, "h", 90));
        else goome_ui_plot_bars(u, label, vals, k, pnum(n, "min", 0), pnum(n, "max", 0), pnum(n, "h", 90));
        break;
    }
    case BT_IMAGE: {
        const goome_bitmap *b = n->bm_ok ? &n->bm : d->placeholder_ok ? &d->placeholder : NULL;
        if (b) goome_ui_image(u, b, pnum(n, "w", 120), pnum(n, "h", 80), pnum(n, "radius", 10));
        hover(d, n);
        break;
    }
    case BT_MENUBAR:
        if (goome_ui_menubar_begin(u)) {
            draw_slot(d, n, 0);
            goome_ui_menubar_end(u);
        }
        return;
    case BT_MENU:
        if (goome_ui_menu_begin(u, label)) {
            draw_slot(d, n, 0);
            goome_ui_menu_end(u);
        }
        return;
    case BT_MENU_ITEM: {
        const char *sc = pstr(n, "shortcut", "");
        if (goome_ui_menu_item(u, label, sc[0] ? sc : NULL, pbool(n, "check") && v ? &v->i : NULL)) fire(d, n, "click");
        return;
    }
    case BT_MENU_SEP: goome_ui_menu_separator(u); return;
    case BT_POPUP:
        if (goome_ui_popup_begin(u, label[0] ? label : "popup")) {
            draw_slot(d, n, 0);
            goome_ui_popup_end(u);
        }
        return;
    case BT_MODAL: {
        if (!v) return;
        const char *b = pstr(n, "button", "");
        if (b[0] && goome_ui_button(u, interp(d, b))) v->i = 1;
        if (goome_ui_modal_begin(u, label[0] ? label : "modal", &v->i)) {
            draw_slot(d, n, 0);
            goome_ui_modal_end(u);
        }
        return;
    }
    }
    const char *tip = pstr(n, "tooltip", "");
    if (leaf && tip[0]) goome_ui_tooltip(u, tip);
}

static void draw_list(bdoc *d, bn **list, int n, int root)
{
    for (int i = 0; i < n; i++) {
        if (root && !d->app && list[i]->type != BT_WINDOW) continue;
        int k = push_style(d, js_get(list[i]->j, "style"));
        draw_node(d, list[i], root);
        if (k) goome_ui_pop(d->u, k);
    }
}

void bdoc_draw(bdoc *d, goome_ui *u, goome *g)
{
    if (!d || !u) return;
    d->u = u, d->g = g;
    if (d->app) {
        if (goome_ui_app(u, d->title, d->app_flags)) {
            draw_list(d, d->top, d->ntop, 1);
            goome_ui_app_end(u);
        }
    } else draw_list(d, d->top, d->ntop, 1);
}
