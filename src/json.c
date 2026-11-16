#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *s, *end;
    int depth;
    char *err;
    int errcap;
} jp;

static void jp_fail(jp *p, const char *why)
{
    if (p->err && p->errcap > 0 && !p->err[0]) snprintf(p->err, (size_t)p->errcap, "%s", why);
}

static void jp_skip(jp *p)
{
    while (p->s < p->end && (*p->s == ' ' || *p->s == '\t' || *p->s == '\n' || *p->s == '\r')) p->s++;
}

static int jp_hex(const char *s, unsigned *out)
{
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return 0;
    }
    *out = v;
    return 1;
}

static int jp_utf8(char *o, unsigned cp)
{
    if (cp < 0x80) return o[0] = (char)cp, 1;
    if (cp < 0x800) return o[0] = (char)(0xC0 | (cp >> 6)), o[1] = (char)(0x80 | (cp & 63)), 2;
    if (cp < 0x10000) return o[0] = (char)(0xE0 | (cp >> 12)), o[1] = (char)(0x80 | ((cp >> 6) & 63)), o[2] = (char)(0x80 | (cp & 63)), 3;
    o[0] = (char)(0xF0 | (cp >> 18)), o[1] = (char)(0x80 | ((cp >> 12) & 63)), o[2] = (char)(0x80 | ((cp >> 6) & 63)), o[3] = (char)(0x80 | (cp & 63));
    return 4;
}

static char *jp_string(jp *p)
{
    const char *start = ++p->s;
    size_t cap = 0;
    while (p->s < p->end && *p->s != '"') {
        if (*p->s == '\\') p->s++;
        p->s++, cap++;
    }
    if (p->s >= p->end) return jp_fail(p, "a string never ends"), NULL;
    char *out = malloc(cap * 2 + 1), *o = out;
    if (!out) return jp_fail(p, "out of memory"), NULL;
    for (const char *s = start; s < p->s; s++) {
        if (*s != '\\') {
            *o++ = *s;
            continue;
        }
        s++;
        switch (*s) {
        case 'n': *o++ = '\n'; break;
        case 't': *o++ = '\t'; break;
        case 'r': *o++ = '\r'; break;
        case 'b': *o++ = '\b'; break;
        case 'f': *o++ = '\f'; break;
        case 'u': {
            unsigned cp, lo;
            if (s + 4 >= p->s || !jp_hex(s + 1, &cp)) {
                free(out);
                return jp_fail(p, "bad \\u escape"), NULL;
            }
            s += 4;
            if (cp >= 0xD800 && cp < 0xDC00 && s + 6 < p->s && s[1] == '\\' && s[2] == 'u' && jp_hex(s + 3, &lo) && lo >= 0xDC00 && lo < 0xE000) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                s += 6;
            }
            o += jp_utf8(o, cp);
            break;
        }
        default: *o++ = *s;
        }
    }
    *o = 0;
    p->s++;
    return out;
}

static int jp_value(jp *p, js *v);

static int jp_push(jp *p, js *v, js *item)
{
    if ((v->n & (v->n - 1)) == 0) {
        js *k = realloc(v->kids, sizeof(js) * (size_t)(v->n ? v->n * 2 : 4));
        if (!k) return jp_fail(p, "out of memory"), 0;
        v->kids = k;
    }
    v->kids[v->n++] = *item;
    return 1;
}

static int jp_list(jp *p, js *v, int obj)
{
    char close = obj ? '}' : ']';
    v->type = obj ? JS_OBJ : JS_ARR;
    p->s++;
    if (++p->depth > 200) return jp_fail(p, "nested way too deep"), 0;
    jp_skip(p);
    if (p->s < p->end && *p->s == close) {
        p->s++, p->depth--;
        return 1;
    }
    for (;;) {
        js item;
        memset(&item, 0, sizeof(item));
        jp_skip(p);
        if (obj) {
            if (p->s >= p->end || *p->s != '"') return jp_fail(p, "expected a key"), 0;
            item.key = jp_string(p);
            if (!item.key) return 0;
            jp_skip(p);
            if (p->s >= p->end || *p->s != ':') {
                free(item.key);
                return jp_fail(p, "expected ':'"), 0;
            }
            p->s++;
        }
        if (!jp_value(p, &item) || !jp_push(p, v, &item)) {
            js_free(&item);
            return 0;
        }
        jp_skip(p);
        if (p->s < p->end && *p->s == ',') {
            p->s++;
            continue;
        }
        if (p->s < p->end && *p->s == close) {
            p->s++, p->depth--;
            return 1;
        }
        return jp_fail(p, obj ? "expected ',' or '}'" : "expected ',' or ']'"), 0;
    }
}

static int jp_word(jp *p, const char *w)
{
    size_t n = strlen(w);
    if ((size_t)(p->end - p->s) < n || memcmp(p->s, w, n)) return 0;
    p->s += n;
    return 1;
}

static int jp_value(jp *p, js *v)
{
    jp_skip(p);
    if (p->s >= p->end) return jp_fail(p, "ends too early"), 0;
    char c = *p->s;
    if (c == '{' || c == '[') return jp_list(p, v, c == '{');
    if (c == '"') {
        v->type = JS_STR;
        v->str = jp_string(p);
        return v->str != NULL;
    }
    if (jp_word(p, "true")) return v->type = JS_BOOL, v->num = 1, 1;
    if (jp_word(p, "false")) return v->type = JS_BOOL, v->num = 0, 1;
    if (jp_word(p, "null")) return v->type = JS_NULL, 1;
    if (c == '-' || (c >= '0' && c <= '9')) {
        char buf[64];
        int k = 0;
        while (p->s < p->end && k < 63 && strchr("+-0123456789.eE", *p->s)) buf[k++] = *p->s++;
        buf[k] = 0;
        v->type = JS_NUM;
        v->num = strtod(buf, NULL);
        return 1;
    }
    return jp_fail(p, "didnt expect that character"), 0;
}

js *js_parse(const char *text, size_t len, char *err, int errcap)
{
    jp p = {text, text + len, 0, err, errcap};
    if (err && errcap > 0) err[0] = 0;
    if (len >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) p.s += 3;
    js *v = calloc(1, sizeof(js));
    if (!v) return NULL;
    if (!jp_value(&p, v)) {
        js_free(v);
        free(v);
        return NULL;
    }
    return v;
}

static void js_clear(js *v)
{
    for (int i = 0; i < v->n; i++) js_clear(&v->kids[i]);
    free(v->kids);
    free(v->str);
    free(v->key);
}

void js_free(js *v)
{
    if (!v) return;
    js_clear(v);
    memset(v, 0, sizeof(*v));
}

const js *js_get(const js *obj, const char *key)
{
    if (!obj || obj->type != JS_OBJ) return NULL;
    for (int i = 0; i < obj->n; i++)
        if (obj->kids[i].key && !strcmp(obj->kids[i].key, key)) return &obj->kids[i];
    return NULL;
}

const js *js_at(const js *arr, int i)
{
    if (!arr || arr->type != JS_ARR || i < 0 || i >= arr->n) return NULL;
    return &arr->kids[i];
}

double js_num(const js *v, double def)
{
    if (!v) return def;
    if (v->type == JS_NUM || v->type == JS_BOOL) return v->num;
    if (v->type == JS_STR && v->str[0]) {
        char *end;
        double d = strtod(v->str, &end);
        if (end != v->str && !*end) return d;
    }
    return def;
}

int js_bool(const js *v, int def)
{
    if (!v) return def;
    if (v->type == JS_BOOL || v->type == JS_NUM) return v->num != 0;
    if (v->type == JS_NULL) return 0;
    if (v->type == JS_STR) return v->str[0] != 0;
    return def;
}

const char *js_str(const js *v, const char *def)
{
    return v && v->type == JS_STR ? v->str : def;
}
