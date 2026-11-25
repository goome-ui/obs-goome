#include "overlay.h"
#include "builder.h"

#include <stdlib.h>
#include <string.h>

static void real_now(struct tm *out)
{
    time_t t = time(NULL);
    struct tm *l = localtime(&t);
    if (l) *out = *l;
    else memset(out, 0, sizeof(*out));
}

static FILE *plain_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

void (*ov_now)(struct tm *out) = real_now;
FILE *(*ov_fopen)(const char *path, const char *mode) = plain_fopen;

typedef struct {
    char path[1024];
    char *text;
    size_t len;
    int ok;
} wfile;

struct ov {
    ov_settings s;
    goome *g;
    goome_ui *u;
    goome_font *sys, *custom;
    char custom_name[256];
    int custom_tried;
    bdoc *doc;
    wfile docf, np, goal, tick;
    char status[1200];
    double poll;
    int running, done;
    double elapsed;
    int need;
    long key;
    double scroll, np_scroll, goal_shown;
    double wx, wy, ww, wh;
    int pushed;
};

static int wfile_poll(wfile *w, const char *path, long max, int tail)
{
    int changed = 0;
    if (strcmp(w->path, path)) {
        snprintf(w->path, sizeof(w->path), "%s", path);
        free(w->text);
        w->text = NULL, w->len = 0, w->ok = 0;
        changed = 1;
    }
    if (!path[0]) return changed;
    FILE *f = ov_fopen(path, "rb");
    if (!f) {
        if (w->ok) {
            free(w->text);
            w->text = NULL, w->len = 0, w->ok = 0;
            changed = 1;
        }
        return changed;
    }
    long size = 0, start = 0;
    if (!fseek(f, 0, SEEK_END)) size = ftell(f);
    if (size < 0) size = 0;
    if (size > max) start = tail ? size - max : 0, size = max;
    fseek(f, start, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return changed;
    }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = 0;
    if (!w->ok || got != w->len || memcmp(buf, w->text, got)) {
        free(w->text);
        w->text = buf, w->len = got, w->ok = 1;
        return 1;
    }
    free(buf);
    return changed;
}

void ov_defaults(ov_settings *s)
{
    memset(s, 0, sizeof(*s));
    s->kind = OV_TIMER;
    s->width = 480, s->height = 200;
    s->theme = -1;
    s->accent = 0xFF6FDC5F;
    s->scale = 1;
    s->animations = 1, s->card = 1;
    s->timer_down = 1, s->timer_seconds = 300, s->timer_buttons = 1;
    snprintf(s->timer_label, sizeof(s->timer_label), "Starting soon");
    snprintf(s->timer_done, sizeof(s->timer_done), "Time!");
    snprintf(s->np_label, sizeof(s->np_label), "Now playing");
    snprintf(s->np_empty, sizeof(s->np_empty), "nothing playing");
    snprintf(s->goal_label, sizeof(s->goal_label), "Sub goal");
    s->goal_current = 12, s->goal_target = 50;
    s->clock_24h = 1, s->clock_seconds = 1, s->clock_date = 1;
    s->ticker_speed = 80, s->ticker_lines = 6;
}

static void apply_theme(ov *o)
{
    goome_theme *t = goome_ui_theme(o->u);
    int preset = o->s.theme;
    if (preset < 0 && o->doc) bdoc_theme(o->doc, t);
    else goome_ui_theme_preset(t, preset < 0 || preset >= GOOME_PRESET_COUNT ? GOOME_PRESET_DARK : preset);
    if (o->s.use_accent) {
        uint32_t a = o->s.accent | 0xFF000000u;
        int lum = (int)(((a >> 16) & 255) * 299 + ((a >> 8) & 255) * 587 + (a & 255) * 114) / 1000;
        t->accent = a;
        t->accent_text = lum > 150 ? 0xFF101010u : 0xFFFFFFFFu;
    }
    if (strcmp(o->custom_name, o->s.font)) {
        goome_font_free(o->custom);
        o->custom = NULL;
        snprintf(o->custom_name, sizeof(o->custom_name), "%s", o->s.font);
        if (o->s.font[0] && strcmp(o->s.font, "builtin")) o->custom = goome_font_system(o->s.font);
    }
    t->font = !strcmp(o->s.font, "builtin") ? NULL : o->custom ? o->custom : o->sys;
    if (!o->s.animations) t->anim_ms = 0, t->hover_ms = t->press_ms = t->popup_ms = t->collapse_ms = t->toast_fade_ms = 0;
    if (o->s.scale > 0 && o->s.scale != 1) goome_ui_scale(o->u, o->s.scale);
}

static void load_doc(ov *o)
{
    goome_theme *t = goome_ui_theme(o->u);
    t->font_heading = t->font_mono = t->font_small = NULL;
    if (o->doc) bdoc_free(o->doc);
    o->doc = NULL;
    if (!o->docf.path[0]) {
        snprintf(o->status, sizeof(o->status), "pick a .goome.json file from the builder in the source settings");
        return;
    }
    if (!o->docf.ok) {
        snprintf(o->status, sizeof(o->status), "cant open %s", o->docf.path);
        return;
    }
    char dir[1024], err[160];
    snprintf(dir, sizeof(dir), "%s", o->docf.path);
    char *slash = strrchr(dir, '/'), *bs = strrchr(dir, '\\');
    if (bs > slash) slash = bs;
    if (slash) slash[1] = 0;
    else dir[0] = 0;
    o->doc = bdoc_parse(o->docf.text, o->docf.len, dir, err, sizeof(err));
    if (!o->doc) snprintf(o->status, sizeof(o->status), "that builder file didnt load: %s", err);
    else if (bdoc_skipped(o->doc)) snprintf(o->status, sizeof(o->status), "loaded %d widgets, skipped %d this plugin cant draw yet", bdoc_count(o->doc), bdoc_skipped(o->doc));
    else snprintf(o->status, sizeof(o->status), "loaded %d widgets", bdoc_count(o->doc));
}

static int poll_files(ov *o)
{
    int ch = 0;
    if (o->s.kind == OV_BUILDER && wfile_poll(&o->docf, o->s.builder_path, (long)32 << 20, 0)) {
        load_doc(o);
        apply_theme(o);
        ch = 1;
    }
    if (o->s.kind == OV_NOW_PLAYING) ch |= wfile_poll(&o->np, o->s.np_path, 8192, 0);
    if (o->s.kind == OV_GOAL && o->s.goal_file) ch |= wfile_poll(&o->goal, o->s.goal_path, 4096, 0);
    if (o->s.kind == OV_TICKER) ch |= wfile_poll(&o->tick, o->s.ticker_path, 65536, 1);
    return ch;
}

ov *ov_new(const ov_settings *s)
{
    ov *o = calloc(1, sizeof(ov));
    if (!o) return NULL;
    int err, w = s->width > 0 ? s->width : 480, h = s->height > 0 ? s->height : 200;
    goome_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.mode = GOOME_MODE_OFFSCREEN, opts.width = w, opts.height = h, opts.scale = 1, opts.title = "goome overlay";
    o->g = goome_open(&opts, &err);
    if (!o->g) {
        free(o);
        return NULL;
    }
    o->u = goome_ui_new(o->g);
    if (!o->u) {
        goome_close(o->g);
        free(o);
        return NULL;
    }
    goome_ui_auto_grab(o->u, 0);
    o->sys = goome_ui_theme(o->u)->font;
    o->s = *s;
    o->s.width = w, o->s.height = h;
    poll_files(o);
    apply_theme(o);
    o->need = 2;
    return o;
}

void ov_free(ov *o)
{
    if (!o) return;
    goome_ui_theme(o->u)->font = o->sys;
    goome_ui_free(o->u);
    goome_close(o->g);
    goome_font_free(o->custom);
    bdoc_free(o->doc);
    free(o->docf.text);
    free(o->np.text);
    free(o->goal.text);
    free(o->tick.text);
    free(o);
}

void ov_set(ov *o, const ov_settings *s)
{
    int w = s->width > 0 ? s->width : 480, h = s->height > 0 ? s->height : 200;
    if (w != o->s.width || h != o->s.height) goome_resize(o->g, w, h, 1);
    if (s->timer_seconds != o->s.timer_seconds || s->timer_down != o->s.timer_down) o->elapsed = 0, o->done = 0;
    if (s->kind != o->s.kind) o->docf.path[0] = 1, o->docf.path[1] = 0;
    o->s = *s;
    o->s.width = w, o->s.height = h;
    poll_files(o);
    apply_theme(o);
    o->need = 2;
}

void ov_push(ov *o, const goome_event *e)
{
    goome_push(o->g, e);
    o->need = 2;
}

void ov_timer(ov *o, int what)
{
    if (what == OV_TIMER_RESET) o->elapsed = 0, o->done = 0, o->running = 0;
    else if (what == OV_TIMER_START) o->running = !o->done;
    else if (what == OV_TIMER_PAUSE) o->running = 0;
    else o->running = !o->running && !o->done;
    o->need = 2;
}

goome *ov_goome(ov *o)
{
    return o ? o->g : NULL;
}

const char *ov_status(const ov *o)
{
    return o ? o->status : "";
}

static goome_font *ufont(ov *o)
{
    return goome_ui_theme(o->u)->font;
}

static double measure(goome_font *f, double size, const char *s)
{
    return goome_text_measure(f, size, s, NULL);
}

static double fit(goome_font *f, const char *s, double size, double maxw)
{
    double w = measure(f, size, s);
    if (w > maxw && w > 0) size *= maxw / w;
    return size;
}

static void text_in(goome_canvas *c, goome_font *f, double size, double x, double y, double w, const char *s, uint32_t col, int align)
{
    double tw = measure(f, size, s);
    if (align == 1) x += (w - tw) / 2;
    else if (align == 2) x += w - tw;
    goome_text(c, f, size, x, y, s, col);
}

static double lh(goome_font *f, double size)
{
    return goome_text_line_height(f, size);
}

static void fmt_num(char *out, size_t cap, double v)
{
    if (v == (double)(long long)v) snprintf(out, cap, "%lld", (long long)v);
    else snprintf(out, cap, "%.2f", v);
}

static int card_begin(ov *o, double *ix, double *iy, double *iw, double *ih)
{
    goome_ui *u = o->u;
    double m = o->s.card ? 10 : 0;
    o->wx = m, o->wy = m, o->ww = o->s.width - 2 * m, o->wh = o->s.height - 2 * m;
    o->pushed = 0;
    if (!o->s.card) {
        goome_ui_push_color(u, GOOME_S_PANEL, 0);
        goome_ui_push_color(u, GOOME_S_BORDER, 0);
        goome_ui_push_color(u, GOOME_S_SHADOW, 0);
        o->pushed = 3;
    }
    if (!goome_ui_window(u, "goome overlay", &o->wx, &o->wy, &o->ww, &o->wh, GOOME_WIN_NO_TITLE, NULL)) return 0;
    goome_ui_item it;
    goome_ui_custom(u, "origin", 1, 1, GOOME_ITEM_NO_HIT, &it);
    *ix = it.x, *iy = it.y;
    *iw = o->ww - 2 * (it.x - o->wx);
    *ih = o->wh - 2 * (it.y - o->wy);
    if (*iw < 1) *iw = 1;
    if (*ih < 1) *ih = 1;
    return 1;
}

static void card_end(ov *o)
{
    goome_ui_window_end(o->u);
    if (o->pushed) goome_ui_pop(o->u, o->pushed);
}

static long timer_shown(ov *o)
{
    long el = (long)o->elapsed, secs = o->s.timer_seconds > 0 ? o->s.timer_seconds : 0;
    if (!o->s.timer_down) return el;
    return el >= secs ? 0 : secs - el;
}

static void draw_timer(ov *o)
{
    double x, y, w, h;
    if (!card_begin(o, &x, &y, &w, &h)) {
        card_end(o);
        return;
    }
    goome_ui *u = o->u;
    const goome_theme *t = goome_ui_theme(u);
    goome_font *f = ufont(o);
    goome_canvas *c = goome_ui_canvas(u);
    double fs = t->font_size, top = y, bottom = y + h;
    if (o->s.timer_label[0] && c) {
        text_in(c, f, fs, x, top, w, o->s.timer_label, t->text_dim, 1);
        top += lh(f, fs) + t->gap * 0.5;
    }
    if (o->s.timer_buttons) {
        double bh = t->item_h > 0 ? t->item_h : lh(f, fs) + 2 * t->pad * 0.7, bw = (w - t->gap) / 2;
        bottom -= bh;
        goome_ui_pos(u, 0, bottom - y);
        goome_ui_size(u, bw, 0);
        if (goome_ui_button(u, o->running ? "Pause" : o->done ? "Done" : "Start")) ov_timer(o, OV_TIMER_TOGGLE);
        goome_ui_pos(u, bw + t->gap, bottom - y);
        goome_ui_size(u, bw, 0);
        if (goome_ui_button(u, "Reset")) ov_timer(o, OV_TIMER_RESET);
        bottom -= t->gap * 0.5;
        c = goome_ui_canvas(u);
    }
    long s = timer_shown(o);
    char buf[160];
    if (o->done && o->s.timer_done[0]) snprintf(buf, sizeof(buf), "%s", o->s.timer_done);
    else if (s >= 3600) snprintf(buf, sizeof(buf), "%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
    else snprintf(buf, sizeof(buf), "%02ld:%02ld", s / 60, s % 60);
    double room = bottom - top, size = fit(f, buf, room * 0.82, w * 0.96);
    uint32_t col = o->done ? t->accent : o->running ? t->text : t->text_dim;
    if (c && size > 1) text_in(c, f, size, x, top + (room - lh(f, size)) / 2, w, buf, col, 1);
    card_end(o);
}

static void split_lines(const char *text, const char **first, size_t *n1, const char **second, size_t *n2)
{
    *first = *second = NULL, *n1 = *n2 = 0;
    const char *p = text;
    for (int k = 0; p && *p && k < 2;) {
        const char *e = strpbrk(p, "\r\n");
        size_t n = e ? (size_t)(e - p) : strlen(p);
        while (n && (p[n - 1] == ' ' || p[n - 1] == '\t')) n--;
        while (n && (*p == ' ' || *p == '\t')) p++, n--;
        if (n) {
            if (!k) *first = p, *n1 = n;
            else *second = p, *n2 = n;
            k++;
        }
        if (!e) break;
        p = e + 1;
    }
}

static int starts_rtl(const char *s, size_t n)
{
    const unsigned char *p = (const unsigned char *)s, *end = p + n;
    while (p < end) {
        uint32_t c = *p;
        int k = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        if (k > 1) c &= 0x3F >> (k - 1);
        for (int i = 1; i < k && p + i < end; i++) c = c << 6 | (p[i] & 0x3F);
        p += k;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0xC0 && c < 0x590 && c != 0xD7 && c != 0xF7)) return 0;
        if ((c >= 0x590 && c < 0x900) || (c >= 0xFB1D && c < 0xFE00) || (c >= 0xFE70 && c < 0xFF00)) return 1;
        if ((c >= 0x900 && c < 0x2000) || (c >= 0x3040 && c < 0xFB00)) return 0;
    }
    return 0;
}

static void marquee(goome_canvas *c, goome_font *f, double size, double x, double y, double w, const char *s, uint32_t col, double off)
{
    double tw = measure(f, size, s);
    if (tw <= w) {
        goome_text(c, f, size, x, y, s, col);
        return;
    }
    double gap = w * 0.4 + size, period = tw + gap;
    if (period < 1) return;
    off -= (double)(long long)(off / period) * period;
    double at = starts_rtl(s, strlen(s)) ? x + w - tw + off : x - off, next = at > x ? at - period : at + period;
    goome_clip_push(c, x, y - size * 0.3, w, lh(f, size) + size * 0.6);
    goome_text(c, f, size, at, y, s, col);
    goome_text(c, f, size, next, y, s, col);
    goome_clip_pop(c);
}

static int np_scrolls(ov *o)
{
    return o->np.ok && o->np.text && o->np.text[0];
}

static void draw_now_playing(ov *o)
{
    double x, y, w, h;
    if (!card_begin(o, &x, &y, &w, &h)) {
        card_end(o);
        return;
    }
    goome_ui *u = o->u;
    const goome_theme *t = goome_ui_theme(u);
    goome_font *f = ufont(o);
    goome_canvas *c = goome_ui_canvas(u);
    if (!c) {
        card_end(o);
        return;
    }
    double icon = h, gap = t->pad;
    if (w > h * 2.6) {
        goome_draw_rrect(c, x, y, icon, icon, goome_radius(t->radius), t->accent);
        double r = icon * 0.13, cx = x + icon * 0.4, cy = y + icon * 0.66, sw = icon * 0.07;
        goome_draw_circle(c, cx, cy, r, t->accent_text);
        goome_draw_rect(c, cx + r - sw, y + icon * 0.24, sw, cy - y - icon * 0.24, t->accent_text);
        goome_draw_rect(c, cx + r - sw, y + icon * 0.24, icon * 0.2, sw * 1.4, t->accent_text);
        x += icon + gap, w -= icon + gap;
    }
    const char *a, *b;
    size_t na, nb;
    char l1[512], l2[512];
    split_lines(o->np.ok ? o->np.text : "", &a, &na, &b, &nb);
    snprintf(l1, sizeof(l1), "%.*s", (int)na, a ? a : "");
    snprintf(l2, sizeof(l2), "%.*s", (int)nb, b ? b : "");
    double fs = t->font_size, ly = y;
    if (o->s.np_label[0]) {
        text_in(c, f, fs * 0.9, x, ly, w, o->s.np_label, t->accent, 0);
        ly += lh(f, fs * 0.9) + 2;
    }
    double rest = y + h - ly, big = rest * (l2[0] ? 0.5 : 0.7);
    if (big > fs * 2.4) big = fs * 2.4;
    if (!l1[0]) {
        text_in(c, f, fs * 1.1, x, ly + (rest - lh(f, fs * 1.1)) / 2, w, o->s.np_empty, t->text_dim, 0);
        card_end(o);
        return;
    }
    double small = big * 0.62, used = lh(f, big) + (l2[0] ? lh(f, small) : 0), ty = ly + (rest - used) / 2;
    marquee(c, f, big, x, ty, w, l1, t->text, o->np_scroll * 45);
    if (l2[0]) marquee(c, f, small, x, ty + lh(f, big), w, l2, t->text_dim, o->np_scroll * 45);
    card_end(o);
}

static int goal_values(ov *o, double *cur, double *target)
{
    *cur = o->s.goal_current, *target = o->s.goal_target;
    if (o->s.goal_file && o->goal.ok && o->goal.text) {
        char *end;
        double a = strtod(o->goal.text, &end);
        if (end == o->goal.text) return 0;
        *cur = a;
        while (*end == ' ' || *end == '/' || *end == ',' || *end == '\t' || *end == 'o' || *end == 'f') end++;
        char *e2;
        double b = strtod(end, &e2);
        if (e2 != end) *target = b;
    }
    return 1;
}

static void draw_goal(ov *o)
{
    double x, y, w, h;
    if (!card_begin(o, &x, &y, &w, &h)) {
        card_end(o);
        return;
    }
    goome_ui *u = o->u;
    const goome_theme *t = goome_ui_theme(u);
    goome_font *f = ufont(o);
    goome_canvas *c = goome_ui_canvas(u);
    if (!c) {
        card_end(o);
        return;
    }
    double cur, target;
    goal_values(o, &cur, &target);
    double frac = target > 0 ? cur / target : 0;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    o->goal_shown = goome_ui_anim(u, "goal", frac, o->s.animations ? 600 : 0, (int)t->ease);
    char a[64], b[64], nums[140], pct[16];
    fmt_num(a, sizeof(a), cur);
    fmt_num(b, sizeof(b), target);
    snprintf(nums, sizeof(nums), "%s / %s", a, b);
    snprintf(pct, sizeof(pct), "%d%%", (int)(frac * 100 + 0.5));
    double fs = t->font_size * 1.15, row = lh(f, fs);
    double nw = measure(f, fs, nums);
    text_in(c, f, fit(f, o->s.goal_label, fs, w - nw - t->gap * 2), x, y, w, o->s.goal_label, t->text, 0);
    text_in(c, f, fs, x, y, w, nums, t->text_dim, 2);
    double by = y + row + t->gap, bh = y + h - by;
    if (bh > 64) by += (bh - 64) / 2, bh = 64;
    if (bh < 6) bh = 6;
    goome_radii r = goome_radius(bh / 2 < t->radius ? bh / 2 : t->radius);
    goome_draw_rrect(c, x, by, w, bh, r, t->input_bg);
    double fw = w * (o->goal_shown < 0 ? 0 : o->goal_shown > 1 ? 1 : o->goal_shown);
    if (fw > 0.5) goome_draw_rrect_grad(c, x, by, fw < bh ? bh : fw, bh, r, t->accent, (t->accent & 0xFF000000u) | ((t->accent & 0xFEFEFE) >> 1) | 0x202020u, 1);
    double ps = bh * 0.5 < fs ? bh * 0.5 : fs;
    if (ps > 6) text_in(c, f, ps, x, by + (bh - lh(f, ps)) / 2, w, pct, o->goal_shown > 0.55 ? t->accent_text : t->text, 1);
    card_end(o);
}

static const char *const wdays[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *const months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static long clock_key(ov *o)
{
    struct tm tm;
    ov_now(&tm);
    return o->s.clock_seconds ? (long)tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec : (long)tm.tm_hour * 60 + tm.tm_min + tm.tm_mday * 10000L;
}

static void draw_clock(ov *o)
{
    double x, y, w, h;
    if (!card_begin(o, &x, &y, &w, &h)) {
        card_end(o);
        return;
    }
    goome_ui *u = o->u;
    const goome_theme *t = goome_ui_theme(u);
    goome_font *f = ufont(o);
    goome_canvas *c = goome_ui_canvas(u);
    if (!c) {
        card_end(o);
        return;
    }
    struct tm tm;
    ov_now(&tm);
    char tbuf[32], dbuf[64];
    int hr = tm.tm_hour;
    if (o->s.clock_24h) {
        if (o->s.clock_seconds) snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d", hr, tm.tm_min, tm.tm_sec);
        else snprintf(tbuf, sizeof(tbuf), "%02d:%02d", hr, tm.tm_min);
    } else {
        int h12 = hr % 12 ? hr % 12 : 12;
        if (o->s.clock_seconds) snprintf(tbuf, sizeof(tbuf), "%d:%02d:%02d %s", h12, tm.tm_min, tm.tm_sec, hr < 12 ? "AM" : "PM");
        else snprintf(tbuf, sizeof(tbuf), "%d:%02d %s", h12, tm.tm_min, hr < 12 ? "AM" : "PM");
    }
    snprintf(dbuf, sizeof(dbuf), "%s %d %s %d", wdays[(tm.tm_wday % 7 + 7) % 7], tm.tm_mday, months[(tm.tm_mon % 12 + 12) % 12], tm.tm_year + 1900);
    double fs = t->font_size, top = y, bottom = y + h;
    if (o->s.clock_label[0]) {
        text_in(c, f, fs, x, top, w, o->s.clock_label, t->text_dim, 1);
        top += lh(f, fs) + t->gap * 0.5;
    }
    double ds = fs * 1.1;
    if (o->s.clock_date) bottom -= lh(f, ds);
    double room = bottom - top, size = fit(f, tbuf, room * 0.85, w * 0.96);
    if (size > 1) text_in(c, f, size, x, top + (room - lh(f, size)) / 2, w, tbuf, t->text, 1);
    if (o->s.clock_date) text_in(c, f, ds, x, bottom, w, dbuf, t->accent, 1);
    card_end(o);
}

static int ticker_lines(const char *text, const char **starts, size_t *lens, int max)
{
    int n = 0;
    if (!text) return 0;
    const char *p = text + strlen(text);
    while (p > text && n < max) {
        const char *e = p;
        while (e > text && (e[-1] == '\n' || e[-1] == '\r')) e--;
        const char *s = e;
        while (s > text && s[-1] != '\n' && s[-1] != '\r') s--;
        if (e > s) starts[n] = s, lens[n] = (size_t)(e - s), n++;
        p = s;
        if (s == text) break;
    }
    return n;
}

static void draw_ticker(ov *o)
{
    double x, y, w, h;
    if (!card_begin(o, &x, &y, &w, &h)) {
        card_end(o);
        return;
    }
    goome_ui *u = o->u;
    const goome_theme *t = goome_ui_theme(u);
    goome_font *f = ufont(o);
    goome_canvas *c = goome_ui_canvas(u);
    if (!c) {
        card_end(o);
        return;
    }
    const char *starts[64];
    size_t lens[64];
    int max = o->s.ticker_chat ? (o->s.ticker_lines > 0 ? o->s.ticker_lines : 6) : 32;
    if (max > 64) max = 64;
    int n = ticker_lines(o->tick.ok ? o->tick.text : NULL, starts, lens, max);
    double fs = t->font_size * 1.15;
    if (o->s.ticker_chat) {
        double row = lh(f, fs) * 1.3, ly = y + h - row;
        goome_clip_push(c, x, y, w, h);
        for (int i = 0; i < n && ly > y - row; i++, ly -= row) {
            char line[600];
            snprintf(line, sizeof(line), "%.*s", (int)(lens[i] < 590 ? lens[i] : 590), starts[i]);
            char *colon = strstr(line, ": ");
            double lx = x;
            if (colon && colon - line < 32) {
                *colon = 0;
                goome_text(c, f, fs, lx, ly, line, t->accent);
                lx += measure(f, fs, line);
                goome_text(c, f, fs, lx, ly, ":", t->text_dim);
                goome_text(c, f, fs, lx + measure(f, fs, ": "), ly, colon + 2, t->text);
            } else goome_text(c, f, fs, lx, ly, line, t->text);
        }
        goome_clip_pop(c);
        if (!n) text_in(c, f, fs, x, y + (h - lh(f, fs)) / 2, w, "no messages yet", t->text_dim, 1);
        card_end(o);
        return;
    }
    if (o->s.ticker_label[0]) {
        double pw = measure(f, fs, o->s.ticker_label) + t->pad * 2;
        goome_draw_rrect(c, x, y, pw, h, goome_radius(t->radius * 0.8), t->accent);
        text_in(c, f, fs, x, y + (h - lh(f, fs)) / 2, pw, o->s.ticker_label, t->accent_text, 1);
        x += pw + t->pad, w -= pw + t->pad;
    }
    const char *sep = f ? "   \xE2\x80\xA2   " : "   -   ", *empty = "nothing in the ticker file yet";
    if (!n) starts[0] = empty, lens[0] = strlen(empty);
    int count = n ? n : 1, rtl = starts_rtl(starts[count - 1], lens[count - 1]);
    double widths[64], sw = measure(f, fs, sep), tw = 0;
    char line[600];
    for (int i = 0; i < count; i++) {
        snprintf(line, sizeof(line), "%.*s", (int)(lens[i] < 590 ? lens[i] : 590), starts[i]);
        widths[i] = measure(f, fs, line);
        tw += widths[i] + (i ? sw : 0);
    }
    double period = tw + w, off = o->scroll * (o->s.ticker_speed > 0 ? o->s.ticker_speed : 80);
    if (period > 1) off -= (double)(long long)(off / period) * period;
    double ty = y + (h - lh(f, fs)) / 2, at = 0;
    goome_clip_push(c, x, y, w, h);
    for (int i = count - 1; i >= 0; i--) {
        for (int part = i == count - 1; part < 2; part++) {
            const char *txt = part ? line : sep;
            double pw = part ? widths[i] : sw;
            if (part) snprintf(line, sizeof(line), "%.*s", (int)(lens[i] < 590 ? lens[i] : 590), starts[i]);
            double px = rtl ? x + off - at - pw : x + w - off + at;
            if (px < x + w && px + pw > x) goome_text(c, f, fs, px, ty, txt, n ? t->text : t->text_dim);
            at += pw;
        }
    }
    goome_clip_pop(c);
    card_end(o);
}

static void draw_builder(ov *o)
{
    if (o->doc) {
        bdoc_draw(o->doc, o->u, o->g);
        return;
    }
    double x, y, w, h;
    if (card_begin(o, &x, &y, &w, &h)) {
        goome_ui_heading(o->u, "goome overlay");
        goome_ui_wrapped(o->u, o->status);
    }
    card_end(o);
}

static int moving(ov *o)
{
    if (o->s.kind == OV_TICKER && !o->s.ticker_chat) return 1;
    if (o->s.kind == OV_NOW_PLAYING && np_scrolls(o)) return 1;
    if (o->s.kind == OV_GOAL) {
        double cur, target;
        goal_values(o, &cur, &target);
        double frac = target > 0 ? cur / target : 0;
        frac = frac < 0 ? 0 : frac > 1 ? 1 : frac;
        return frac != o->goal_shown;
    }
    return 0;
}

int ov_tick(ov *o, double dt)
{
    if (!o) return 0;
    if (dt < 0) dt = 0;
    if (dt > 1) dt = 1;
    int draw = 0;
    o->poll += dt;
    if (o->poll >= 1) {
        o->poll = 0;
        draw |= poll_files(o);
    }
    if (o->running) {
        o->elapsed += dt;
        if (o->s.timer_down && o->elapsed >= o->s.timer_seconds) o->running = 0, o->done = 1, draw = 1;
    }
    long key = o->s.kind == OV_TIMER ? timer_shown(o) * 4 + o->running * 2 + o->done : o->s.kind == OV_CLOCK ? clock_key(o) : 0;
    if (key != o->key) o->key = key, draw = 1;
    if (moving(o)) {
        o->scroll += dt, o->np_scroll += dt;
        draw = 1;
    }
    goome_event ev;
    while (goome_poll(o->g, &ev, 0) > 0) {
        goome_ui_feed(o->u, &ev);
        draw = 1;
    }
    if (o->need > 0) o->need--, draw = 1;
    if (goome_ui_animating(o->u)) draw = 1;
    if (!draw) return 0;
    if (!goome_ui_begin(o->u, 0)) return 0;
    switch (o->s.kind) {
    case OV_BUILDER: draw_builder(o); break;
    case OV_TIMER: draw_timer(o); break;
    case OV_NOW_PLAYING: draw_now_playing(o); break;
    case OV_GOAL: draw_goal(o); break;
    case OV_CLOCK: draw_clock(o); break;
    default: draw_ticker(o); break;
    }
    goome_ui_end(o->u);
    return 1;
}
