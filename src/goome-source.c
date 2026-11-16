#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
typedef CRITICAL_SECTION glock;
#define glock_init(l) InitializeCriticalSection(l)
#define glock_free(l) DeleteCriticalSection(l)
#define glock_on(l) EnterCriticalSection(l)
#define glock_off(l) LeaveCriticalSection(l)
#else
#include <pthread.h>
typedef pthread_mutex_t glock;
#define glock_init(l) pthread_mutex_init(l, NULL)
#define glock_free(l) pthread_mutex_destroy(l)
#define glock_on(l) pthread_mutex_lock(l)
#define glock_off(l) pthread_mutex_unlock(l)
#endif

#include <stdlib.h>
#include <string.h>

#include "goome-source.h"
#include "overlay.h"

#define T(s) obs_module_text(s)

typedef struct {
    obs_source_t *source;
    ov *o;
    glock lock;
    ov_settings set, pending;
    int has_pending;
    gs_texture_t *tex;
    uint8_t *buf;
    int bw, bh, tw, th;
    int upload;
    char status[1200];
} gsrc;

static uint32_t from_obs_color(long long c)
{
    uint32_t v = (uint32_t)c;
    return (v & 0xFF000000u) | ((v & 255) << 16) | (v & 0xFF00u) | ((v >> 16) & 255);
}

static long long to_obs_color(uint32_t argb)
{
    return (long long)((argb & 0xFF000000u) | ((argb & 255) << 16) | (argb & 0xFF00u) | ((argb >> 16) & 255));
}

static void copy_str(char *dst, size_t cap, const char *s)
{
    snprintf(dst, cap, "%s", s ? s : "");
}

static void read_settings(obs_data_t *d, ov_settings *s)
{
    ov_defaults(s);
    s->kind = (int)obs_data_get_int(d, "kind");
    s->width = (int)obs_data_get_int(d, "width");
    s->height = (int)obs_data_get_int(d, "height");
    s->theme = (int)obs_data_get_int(d, "theme");
    s->use_accent = obs_data_get_bool(d, "use_accent");
    s->accent = from_obs_color(obs_data_get_int(d, "accent"));
    s->scale = obs_data_get_double(d, "scale");
    copy_str(s->font, sizeof(s->font), obs_data_get_string(d, "font"));
    s->animations = obs_data_get_bool(d, "animations");
    s->card = obs_data_get_bool(d, "card");
    copy_str(s->builder_path, sizeof(s->builder_path), obs_data_get_string(d, "builder_file"));
    s->timer_down = obs_data_get_int(d, "timer_mode") == 1;
    s->timer_seconds = (int)(obs_data_get_int(d, "timer_minutes") * 60 + obs_data_get_int(d, "timer_secs"));
    s->timer_buttons = obs_data_get_bool(d, "timer_buttons");
    copy_str(s->timer_label, sizeof(s->timer_label), obs_data_get_string(d, "timer_label"));
    copy_str(s->timer_done, sizeof(s->timer_done), obs_data_get_string(d, "timer_done"));
    copy_str(s->np_path, sizeof(s->np_path), obs_data_get_string(d, "np_file"));
    copy_str(s->np_label, sizeof(s->np_label), obs_data_get_string(d, "np_label"));
    copy_str(s->np_empty, sizeof(s->np_empty), obs_data_get_string(d, "np_empty"));
    copy_str(s->goal_label, sizeof(s->goal_label), obs_data_get_string(d, "goal_label"));
    s->goal_file = obs_data_get_int(d, "goal_source") == 1;
    copy_str(s->goal_path, sizeof(s->goal_path), obs_data_get_string(d, "goal_file"));
    s->goal_current = obs_data_get_double(d, "goal_current");
    s->goal_target = obs_data_get_double(d, "goal_target");
    s->clock_24h = obs_data_get_bool(d, "clock_24h");
    s->clock_seconds = obs_data_get_bool(d, "clock_seconds");
    s->clock_date = obs_data_get_bool(d, "clock_date");
    copy_str(s->clock_label, sizeof(s->clock_label), obs_data_get_string(d, "clock_label"));
    copy_str(s->ticker_path, sizeof(s->ticker_path), obs_data_get_string(d, "ticker_file"));
    copy_str(s->ticker_label, sizeof(s->ticker_label), obs_data_get_string(d, "ticker_label"));
    s->ticker_chat = obs_data_get_int(d, "ticker_style") == 1;
    s->ticker_speed = obs_data_get_double(d, "ticker_speed");
    s->ticker_lines = (int)obs_data_get_int(d, "ticker_lines");
    if (s->width < 16) s->width = 16;
    if (s->height < 16) s->height = 16;
    if (s->width > 4096) s->width = 4096;
    if (s->height > 4096) s->height = 4096;
}

static const char *src_name(void *type_data)
{
    (void)type_data;
    return T("GoomeOverlay");
}

static void src_defaults(obs_data_t *d)
{
    ov_settings s;
    ov_defaults(&s);
    obs_data_set_default_int(d, "kind", s.kind);
    obs_data_set_default_int(d, "width", s.width);
    obs_data_set_default_int(d, "height", s.height);
    obs_data_set_default_int(d, "theme", s.theme);
    obs_data_set_default_bool(d, "use_accent", false);
    obs_data_set_default_int(d, "accent", to_obs_color(s.accent));
    obs_data_set_default_double(d, "scale", 1.0);
    obs_data_set_default_string(d, "font", "");
    obs_data_set_default_bool(d, "animations", true);
    obs_data_set_default_bool(d, "card", true);
    obs_data_set_default_int(d, "timer_mode", 1);
    obs_data_set_default_int(d, "timer_minutes", s.timer_seconds / 60);
    obs_data_set_default_int(d, "timer_secs", s.timer_seconds % 60);
    obs_data_set_default_bool(d, "timer_buttons", true);
    obs_data_set_default_string(d, "timer_label", s.timer_label);
    obs_data_set_default_string(d, "timer_done", s.timer_done);
    obs_data_set_default_string(d, "np_label", s.np_label);
    obs_data_set_default_string(d, "np_empty", s.np_empty);
    obs_data_set_default_string(d, "goal_label", s.goal_label);
    obs_data_set_default_int(d, "goal_source", 0);
    obs_data_set_default_double(d, "goal_current", s.goal_current);
    obs_data_set_default_double(d, "goal_target", s.goal_target);
    obs_data_set_default_bool(d, "clock_24h", true);
    obs_data_set_default_bool(d, "clock_seconds", true);
    obs_data_set_default_bool(d, "clock_date", true);
    obs_data_set_default_string(d, "clock_label", "");
    obs_data_set_default_int(d, "ticker_style", 0);
    obs_data_set_default_double(d, "ticker_speed", s.ticker_speed);
    obs_data_set_default_int(d, "ticker_lines", s.ticker_lines);
    obs_data_set_default_string(d, "ticker_label", "");
}

static FILE *utf8_fopen(const char *path, const char *mode)
{
    return os_fopen(path, mode);
}

static void *src_create(obs_data_t *settings, obs_source_t *source)
{
    gsrc *s = bzalloc(sizeof(gsrc));
    s->source = source;
    ov_fopen = utf8_fopen;
    read_settings(settings, &s->set);
    s->o = ov_new(&s->set);
    if (!s->o) {
        obs_log(LOG_WARNING, "goome couldnt open an offscreen ui");
        bfree(s);
        return NULL;
    }
    glock_init(&s->lock);
    return s;
}

static void src_destroy(void *data)
{
    gsrc *s = data;
    if (s->tex) {
        obs_enter_graphics();
        gs_texture_destroy(s->tex);
        obs_leave_graphics();
    }
    ov_free(s->o);
    glock_free(&s->lock);
    bfree(s->buf);
    bfree(s);
}

static void src_update(void *data, obs_data_t *settings)
{
    gsrc *s = data;
    ov_settings next;
    read_settings(settings, &next);
    glock_on(&s->lock);
    s->pending = next;
    s->has_pending = 1;
    glock_off(&s->lock);
}

static uint32_t src_width(void *data)
{
    gsrc *s = data;
    return (uint32_t)s->set.width;
}

static uint32_t src_height(void *data)
{
    gsrc *s = data;
    return (uint32_t)s->set.height;
}

static void grab_frame(gsrc *s)
{
    goome *g = ov_goome(s->o);
    goome_image img;
    goome_rect r;
    if (goome_frame(g, &img) != GOOME_OK || img.w <= 0 || img.h <= 0) return;
    if (img.w != s->bw || img.h != s->bh || !s->buf) {
        bfree(s->buf);
        s->buf = bzalloc((size_t)img.w * (size_t)img.h * 4);
        s->bw = img.w, s->bh = img.h;
        goome_frame_copy(g, s->buf, s->bw * 4, GOOME_FMT_BGRA_PREMUL, NULL);
        goome_frame_dirty(g, &r);
        s->upload = 1;
        return;
    }
    if (!goome_frame_dirty(g, &r)) return;
    if (r.x < 0 || r.y < 0 || r.w <= 0 || r.h <= 0 || r.x + r.w > s->bw || r.y + r.h > s->bh) return;
    goome_frame_copy(g, s->buf + ((size_t)r.y * (size_t)s->bw + (size_t)r.x) * 4, s->bw * 4, GOOME_FMT_BGRA_PREMUL, &r);
    s->upload = 1;
}

static void src_tick(void *data, float seconds)
{
    gsrc *s = data;
    glock_on(&s->lock);
    if (s->has_pending) {
        s->set = s->pending;
        s->has_pending = 0;
        ov_set(s->o, &s->set);
    }
    if (ov_tick(s->o, seconds)) grab_frame(s);
    const char *st = ov_status(s->o);
    if (strcmp(st, s->status)) {
        copy_str(s->status, sizeof(s->status), st);
        if (st[0]) obs_log(LOG_INFO, "%s", st);
    }
    glock_off(&s->lock);
}

static void src_render(void *data, gs_effect_t *unused)
{
    gsrc *s = data;
    (void)unused;
    glock_on(&s->lock);
    if (!s->buf) {
        glock_off(&s->lock);
        return;
    }
    if (!s->tex || s->tw != s->bw || s->th != s->bh) {
        if (s->tex) gs_texture_destroy(s->tex);
        s->tex = gs_texture_create((uint32_t)s->bw, (uint32_t)s->bh, GS_BGRA, 1, NULL, GS_DYNAMIC);
        s->tw = s->bw, s->th = s->bh;
        s->upload = 1;
    }
    if (s->tex && s->upload) {
        gs_texture_set_image(s->tex, s->buf, (uint32_t)s->bw * 4, false);
        s->upload = 0;
    }
    gs_texture_t *tex = s->tex;
    glock_off(&s->lock);
    if (!tex) return;
    gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_PREMULTIPLIED_ALPHA);
    bool srgb = gs_framebuffer_srgb_enabled();
    gs_enable_framebuffer_srgb(false);
    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
    gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), tex);
    while (gs_effect_loop(effect, "Draw")) obs_source_draw(tex, 0, 0, 0, 0, false);
    gs_blend_state_pop();
    gs_enable_framebuffer_srgb(srgb);
}

static int mods_of(uint32_t m)
{
    int k = 0;
    if (m & INTERACT_SHIFT_KEY) k |= GOOME_SHIFT;
    if (m & INTERACT_CONTROL_KEY) k |= GOOME_CTRL;
    if (m & INTERACT_ALT_KEY) k |= GOOME_ALT;
    if (m & INTERACT_COMMAND_KEY) k |= GOOME_SUPER;
    return k;
}

static void push(gsrc *s, const goome_event *e)
{
    glock_on(&s->lock);
    ov_push(s->o, e);
    glock_off(&s->lock);
}

static void src_mouse_click(void *data, const struct obs_mouse_event *event, int32_t type, bool mouse_up, uint32_t clicks)
{
    goome_event e;
    (void)clicks;
    memset(&e, 0, sizeof(e));
    e.type = mouse_up ? GOOME_UP : GOOME_DOWN;
    e.x = event->x, e.y = event->y;
    e.button = type == MOUSE_LEFT ? 1 : type == MOUSE_MIDDLE ? 2 : 3;
    e.mods = mods_of(event->modifiers);
    push(data, &e);
}

static void src_mouse_move(void *data, const struct obs_mouse_event *event, bool mouse_leave)
{
    goome_event e;
    memset(&e, 0, sizeof(e));
    e.type = GOOME_MOTION;
    e.x = mouse_leave ? -100000 : event->x;
    e.y = mouse_leave ? -100000 : event->y;
    e.mods = mods_of(event->modifiers);
    push(data, &e);
}

static void src_mouse_wheel(void *data, const struct obs_mouse_event *event, int x_delta, int y_delta)
{
    goome_event e;
    (void)x_delta;
    if (!y_delta) return;
    memset(&e, 0, sizeof(e));
    e.type = GOOME_SCROLL;
    e.x = event->x, e.y = event->y;
    int notches = (y_delta < 0 ? -y_delta : y_delta) / 120;
    if (notches < 1) notches = 1;
    e.dy = y_delta > 0 ? -notches : notches;
    e.mods = mods_of(event->modifiers);
    push(data, &e);
}

static int special_key(uint32_t vkey)
{
#if defined(_WIN32)
    switch (vkey) {
    case 0x08: return GOOME_K_BACKSPACE;
    case 0x09: return GOOME_K_TAB;
    case 0x0D: return GOOME_K_ENTER;
    case 0x1B: return GOOME_K_ESC;
    case 0x2E: return GOOME_K_DELETE;
    case 0x25: return GOOME_K_LEFT;
    case 0x26: return GOOME_K_UP;
    case 0x27: return GOOME_K_RIGHT;
    case 0x28: return GOOME_K_DOWN;
    case 0x24: return GOOME_K_HOME;
    case 0x23: return GOOME_K_END;
    case 0x21: return GOOME_K_PGUP;
    case 0x22: return GOOME_K_PGDN;
    case 0x2D: return GOOME_K_INSERT;
    }
    if (vkey >= 0x41 && vkey <= 0x5A) return (int)(vkey - 0x41 + 'a');
    if (vkey >= 0x30 && vkey <= 0x39) return (int)vkey;
#elif defined(__APPLE__)
    switch (vkey) {
    case 51: return GOOME_K_BACKSPACE;
    case 48: return GOOME_K_TAB;
    case 36: case 76: return GOOME_K_ENTER;
    case 53: return GOOME_K_ESC;
    case 117: return GOOME_K_DELETE;
    case 123: return GOOME_K_LEFT;
    case 124: return GOOME_K_RIGHT;
    case 125: return GOOME_K_DOWN;
    case 126: return GOOME_K_UP;
    case 115: return GOOME_K_HOME;
    case 119: return GOOME_K_END;
    case 116: return GOOME_K_PGUP;
    case 121: return GOOME_K_PGDN;
    }
#else
    switch (vkey) {
    case 0xFF08: return GOOME_K_BACKSPACE;
    case 0xFF09: case 0xFE20: return GOOME_K_TAB;
    case 0xFF0D: case 0xFF8D: return GOOME_K_ENTER;
    case 0xFF1B: return GOOME_K_ESC;
    case 0xFFFF: return GOOME_K_DELETE;
    case 0xFF51: return GOOME_K_LEFT;
    case 0xFF52: return GOOME_K_UP;
    case 0xFF53: return GOOME_K_RIGHT;
    case 0xFF54: return GOOME_K_DOWN;
    case 0xFF50: return GOOME_K_HOME;
    case 0xFF57: return GOOME_K_END;
    case 0xFF55: return GOOME_K_PGUP;
    case 0xFF56: return GOOME_K_PGDN;
    case 0xFF63: return GOOME_K_INSERT;
    }
    if (vkey >= 'a' && vkey <= 'z') return (int)vkey;
    if (vkey >= 'A' && vkey <= 'Z') return (int)(vkey - 'A' + 'a');
    if (vkey >= '0' && vkey <= '9') return (int)vkey;
#endif
    return 0;
}

static void src_key_click(void *data, const struct obs_key_event *event, bool key_up)
{
    goome_event e;
    if (key_up) return;
    memset(&e, 0, sizeof(e));
    e.type = GOOME_KEY;
    e.mods = mods_of(event->modifiers);
    const unsigned char *t = (const unsigned char *)(event->text ? event->text : "");
    int key = special_key(event->native_vkey);
    if (!key && t[0] >= 0x20 && t[0] != 0x7F) key = t[0] < 0x80 ? (t[0] >= 'A' && t[0] <= 'Z' ? t[0] - 'A' + 'a' : t[0]) : t[0];
    if (!key && t[0] && t[0] < 0x20 && (e.mods & GOOME_CTRL)) key = t[0] + 'a' - 1;
    if (!key) return;
    e.key = key;
    if (t[0] >= 0x20 && t[0] != 0x7F && !(e.mods & (GOOME_CTRL | GOOME_SUPER)) && strlen((const char *)t) < sizeof(e.text)) copy_str(e.text, sizeof(e.text), (const char *)t);
    push(data, &e);
}

static void src_focus(void *data, bool focus)
{
    goome_event e;
    memset(&e, 0, sizeof(e));
    e.type = GOOME_FOCUS;
    e.id = focus ? 1 : 0;
    push(data, &e);
}

static bool timer_button(obs_properties_t *props, obs_property_t *p, void *data)
{
    gsrc *s = data;
    (void)props;
    if (!s) return false;
    glock_on(&s->lock);
    ov_timer(s->o, strcmp(obs_property_name(p), "timer_reset") ? OV_TIMER_TOGGLE : OV_TIMER_RESET);
    glock_off(&s->lock);
    return false;
}

static const char *const groups[OV_KIND_COUNT] = {"builder_group", "timer_group", "np_group", "goal_group", "clock_group", "ticker_group"};

static bool kind_changed(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
    (void)p;
    int kind = (int)obs_data_get_int(settings, "kind");
    for (int i = 0; i < OV_KIND_COUNT; i++) obs_property_set_visible(obs_properties_get(props, groups[i]), i == kind);
    obs_property_set_visible(obs_properties_get(props, "goal_file"), obs_data_get_int(settings, "goal_source") == 1);
    obs_property_set_visible(obs_properties_get(props, "goal_current"), obs_data_get_int(settings, "goal_source") == 0);
    obs_property_set_visible(obs_properties_get(props, "ticker_lines"), obs_data_get_int(settings, "ticker_style") == 1);
    obs_property_set_visible(obs_properties_get(props, "ticker_speed"), obs_data_get_int(settings, "ticker_style") == 0);
    obs_property_set_visible(obs_properties_get(props, "accent"), obs_data_get_bool(settings, "use_accent"));
    return true;
}

static obs_properties_t *src_properties(void *data)
{
    obs_properties_t *props = obs_properties_create();
    obs_property_t *p = obs_properties_add_list(props, "kind", T("Kind"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(p, T("Kind.Builder"), OV_BUILDER);
    obs_property_list_add_int(p, T("Kind.Timer"), OV_TIMER);
    obs_property_list_add_int(p, T("Kind.NowPlaying"), OV_NOW_PLAYING);
    obs_property_list_add_int(p, T("Kind.Goal"), OV_GOAL);
    obs_property_list_add_int(p, T("Kind.Clock"), OV_CLOCK);
    obs_property_list_add_int(p, T("Kind.Ticker"), OV_TICKER);
    obs_property_set_modified_callback(p, kind_changed);
    obs_properties_add_int(props, "width", T("Width"), 16, 4096, 1);
    obs_properties_add_int(props, "height", T("Height"), 16, 4096, 1);

    obs_properties_t *b = obs_properties_create();
    obs_properties_add_path(b, "builder_file", T("BuilderFile"), OBS_PATH_FILE, "goome builder (*.goome.json *.json)", NULL);
    obs_properties_add_text(b, "builder_help", T("BuilderHelp"), OBS_TEXT_INFO);
    obs_properties_add_group(props, "builder_group", T("Kind.Builder"), OBS_GROUP_NORMAL, b);

    obs_properties_t *t = obs_properties_create();
    p = obs_properties_add_list(t, "timer_mode", T("TimerMode"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(p, T("TimerMode.Up"), 0);
    obs_property_list_add_int(p, T("TimerMode.Down"), 1);
    obs_properties_add_int(t, "timer_minutes", T("TimerMinutes"), 0, 6000, 1);
    obs_properties_add_int(t, "timer_secs", T("TimerSeconds"), 0, 59, 1);
    obs_properties_add_text(t, "timer_label", T("Label"), OBS_TEXT_DEFAULT);
    obs_properties_add_text(t, "timer_done", T("TimerDone"), OBS_TEXT_DEFAULT);
    obs_properties_add_bool(t, "timer_buttons", T("TimerButtons"));
    obs_properties_add_button2(t, "timer_start", T("TimerStartPause"), timer_button, data);
    obs_properties_add_button2(t, "timer_reset", T("TimerReset"), timer_button, data);
    obs_properties_add_group(props, "timer_group", T("Kind.Timer"), OBS_GROUP_NORMAL, t);

    obs_properties_t *n = obs_properties_create();
    obs_properties_add_path(n, "np_file", T("NowPlayingFile"), OBS_PATH_FILE, "Text (*.txt);;All files (*.*)", NULL);
    obs_properties_add_text(n, "np_label", T("Label"), OBS_TEXT_DEFAULT);
    obs_properties_add_text(n, "np_empty", T("NowPlayingEmpty"), OBS_TEXT_DEFAULT);
    obs_properties_add_group(props, "np_group", T("Kind.NowPlaying"), OBS_GROUP_NORMAL, n);

    obs_properties_t *gp = obs_properties_create();
    obs_properties_add_text(gp, "goal_label", T("Label"), OBS_TEXT_DEFAULT);
    p = obs_properties_add_list(gp, "goal_source", T("GoalSource"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(p, T("GoalSource.Settings"), 0);
    obs_property_list_add_int(p, T("GoalSource.File"), 1);
    obs_property_set_modified_callback(p, kind_changed);
    obs_properties_add_float(gp, "goal_current", T("GoalCurrent"), -1e9, 1e9, 1);
    obs_properties_add_float(gp, "goal_target", T("GoalTarget"), 0, 1e9, 1);
    obs_properties_add_path(gp, "goal_file", T("GoalFile"), OBS_PATH_FILE, "Text (*.txt);;All files (*.*)", NULL);
    obs_properties_add_group(props, "goal_group", T("Kind.Goal"), OBS_GROUP_NORMAL, gp);

    obs_properties_t *c = obs_properties_create();
    obs_properties_add_bool(c, "clock_24h", T("Clock24h"));
    obs_properties_add_bool(c, "clock_seconds", T("ClockSeconds"));
    obs_properties_add_bool(c, "clock_date", T("ClockDate"));
    obs_properties_add_text(c, "clock_label", T("Label"), OBS_TEXT_DEFAULT);
    obs_properties_add_group(props, "clock_group", T("Kind.Clock"), OBS_GROUP_NORMAL, c);

    obs_properties_t *k = obs_properties_create();
    obs_properties_add_path(k, "ticker_file", T("TickerFile"), OBS_PATH_FILE, "Text (*.txt *.log);;All files (*.*)", NULL);
    p = obs_properties_add_list(k, "ticker_style", T("TickerStyle"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(p, T("TickerStyle.Scroll"), 0);
    obs_property_list_add_int(p, T("TickerStyle.Chat"), 1);
    obs_property_set_modified_callback(p, kind_changed);
    obs_properties_add_text(k, "ticker_label", T("Label"), OBS_TEXT_DEFAULT);
    obs_properties_add_float_slider(k, "ticker_speed", T("TickerSpeed"), 10, 600, 5);
    obs_properties_add_int(k, "ticker_lines", T("TickerLines"), 1, 40, 1);
    obs_properties_add_group(props, "ticker_group", T("Kind.Ticker"), OBS_GROUP_NORMAL, k);

    p = obs_properties_add_list(props, "theme", T("Theme"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(p, T("Theme.File"), -1);
    obs_property_list_add_int(p, T("Theme.Dark"), GOOME_PRESET_DARK);
    obs_property_list_add_int(p, T("Theme.Light"), GOOME_PRESET_LIGHT);
    obs_property_list_add_int(p, T("Theme.Midnight"), GOOME_PRESET_MIDNIGHT);
    obs_property_list_add_int(p, T("Theme.Paper"), GOOME_PRESET_PAPER);
    obs_property_list_add_int(p, T("Theme.Mint"), GOOME_PRESET_MINT);
    obs_property_list_add_int(p, T("Theme.Contrast"), GOOME_PRESET_HIGH_CONTRAST);
    obs_property_list_add_int(p, T("Theme.Rose"), GOOME_PRESET_ROSE);
    p = obs_properties_add_bool(props, "use_accent", T("UseAccent"));
    obs_property_set_modified_callback(p, kind_changed);
    obs_properties_add_color_alpha(props, "accent", T("Accent"));
    obs_properties_add_float_slider(props, "scale", T("Scale"), 0.5, 4, 0.05);
    obs_properties_add_text(props, "font", T("Font"), OBS_TEXT_DEFAULT);
    obs_properties_add_bool(props, "card", T("Card"));
    obs_properties_add_bool(props, "animations", T("Animations"));
    return props;
}

static struct obs_source_info goome_overlay_info;

void goome_overlay_register(void)
{
    memset(&goome_overlay_info, 0, sizeof(goome_overlay_info));
    goome_overlay_info.id = "goome_overlay";
    goome_overlay_info.type = OBS_SOURCE_TYPE_INPUT;
    goome_overlay_info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_INTERACTION;
    goome_overlay_info.icon_type = OBS_ICON_TYPE_TEXT;
    goome_overlay_info.get_name = src_name;
    goome_overlay_info.create = src_create;
    goome_overlay_info.destroy = src_destroy;
    goome_overlay_info.update = src_update;
    goome_overlay_info.get_defaults = src_defaults;
    goome_overlay_info.get_properties = src_properties;
    goome_overlay_info.get_width = src_width;
    goome_overlay_info.get_height = src_height;
    goome_overlay_info.video_tick = src_tick;
    goome_overlay_info.video_render = src_render;
    goome_overlay_info.mouse_click = src_mouse_click;
    goome_overlay_info.mouse_move = src_mouse_move;
    goome_overlay_info.mouse_wheel = src_mouse_wheel;
    goome_overlay_info.key_click = src_key_click;
    goome_overlay_info.focus = src_focus;
    obs_register_source(&goome_overlay_info);
}
