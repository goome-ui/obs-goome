#include "goome-source.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shim.h"

typedef struct {
    const char *name;
    uint32_t hash;
    int w, h;
} result;

static const struct obs_source_info *info;
static const char *root = ".";
static int shots;
static result results[64];
static int nresults, failures;

static void fixed_now(struct tm *t)
{
    memset(t, 0, sizeof(*t));
    t->tm_year = 126, t->tm_mon = 10, t->tm_mday = 15, t->tm_wday = 0;
    t->tm_hour = 19, t->tm_min = 42, t->tm_sec = 7;
}

static uint32_t fnv(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "cant write %s\n", path);
        exit(1);
    }
    fputs(text, f);
    fclose(f);
}

static obs_data_t *base(int kind, int w, int h)
{
    obs_data_t *d = shim_data_new();
    info->get_defaults(d);
    shim_set_int(d, "kind", kind);
    shim_set_int(d, "width", w);
    shim_set_int(d, "height", h);
    shim_set_string(d, "font", shots ? "" : "builtin");
    shim_set_bool(d, "animations", 0);
    return d;
}

static void frames(void *src, int n, float dt)
{
    for (int i = 0; i < n; i++) {
        info->video_tick(src, dt);
        info->video_render(src, NULL);
    }
}

static void mouse(void *src, int type, int x, int y)
{
    struct obs_mouse_event e = {0, x, y};
    if (type == GOOME_MOTION) info->mouse_move(src, &e, false);
    else info->mouse_click(src, &e, MOUSE_LEFT, type == GOOME_UP, 1);
}

static void click(void *src, int x, int y)
{
    mouse(src, GOOME_MOTION, x, y);
    frames(src, 1, 1.0f / 60);
    mouse(src, GOOME_DOWN, x, y);
    mouse(src, GOOME_UP, x, y);
    frames(src, 1, 1.0f / 60);
}

static void leave(void *src)
{
    struct obs_mouse_event e = {0, 0, 0};
    info->mouse_move(src, &e, true);
    frames(src, 2, 1.0f / 60);
}

static void type(void *src, const char *s)
{
    for (; *s; s++) {
        char text[2] = {*s, 0};
        struct obs_key_event e;
        memset(&e, 0, sizeof(e));
        e.text = text;
#if defined(_WIN32)
        e.native_vkey = (uint32_t)(*s >= 'a' && *s <= 'z' ? *s - 'a' + 'A' : *s);
#elif !defined(__APPLE__)
        e.native_vkey = (uint32_t)*s;
#endif
        info->key_click(src, &e, false);
        info->key_click(src, &e, true);
    }
    frames(src, 1, 1.0f / 60);
}

static void save_shot(const char *name, const uint8_t *px, int w, int h)
{
    char path[1200];
    snprintf(path, sizeof(path), "%s.ppm", name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "cant write %s\n", path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const uint8_t *p = px + ((size_t)y * w + x) * 4;
            int t = (x + y) * 255 / (w + h);
            int bg[3] = {0x24 + (0x0E - 0x24) * t / 255, 0x16 + (0x3A - 0x16) * t / 255, 0x3E + (0x4A - 0x3E) * t / 255};
            unsigned char o[3];
            for (int k = 0; k < 3; k++) o[k] = (unsigned char)(p[2 - k] + bg[k] * (255 - p[3]) / 255);
            fwrite(o, 1, 3, f);
        }
    fclose(f);
}

static uint32_t snap(void *src, const char *name)
{
    gsrc *s = src;
    int w, h;
    const uint8_t *tex = shim_texture(&w, &h);
    if (!tex || w != s->set.width || h != s->set.height) {
        printf("FAIL %s: no texture or the wrong size\n", name);
        failures++;
        return 0;
    }
    size_t n = (size_t)w * h * 4;
    uint8_t *full = malloc(n);
    if (!full) exit(1);
    goome_frame_copy(ov_goome(s->o), full, w * 4, GOOME_FMT_BGRA_PREMUL, NULL);
    if (memcmp(full, tex, n) || memcmp(full, s->buf, n)) {
        printf("FAIL %s: the texture built from dirty rects doesnt match the whole frame\n", name);
        failures++;
    }
    free(full);
    uint32_t hash = fnv(tex, n);
    if (nresults < 64) results[nresults].name = name, results[nresults].hash = hash, results[nresults].w = w, results[nresults++].h = h;
    if (shots) save_shot(name, tex, w, h);
    return hash;
}

static void expect(int ok, const char *what)
{
    if (ok) return;
    printf("FAIL %s\n", what);
    failures++;
}

static void test_timer(void)
{
    obs_data_t *d = base(OV_TIMER, 480, 200);
    void *a = info->create(d, NULL);
    frames(a, 3, 1.0f / 60);
    uint32_t fresh = snap(a, "timer");
    click(a, 126, 160);
    leave(a);
    frames(a, 65, 1.0f);
    uint32_t clicked = snap(a, "timer_running");

    void *b = info->create(d, NULL);
    obs_properties_t *props = info->get_properties(b);
    expect(shim_click(props, "timer_start"), "timer has a start button in its properties");
    frames(b, 3, 1.0f / 60);
    frames(b, 65, 1.0f);
    uint32_t pressed = snap(b, "timer_props");
    expect(clicked == pressed, "clicking Start through Interact draws the same as the Start button in the properties");
    expect(fresh != clicked, "the timer moved after it got started");
    shim_click(props, "timer_reset");
    frames(b, 2, 1.0f / 60);
    expect(snap(b, "timer_reset") == fresh, "reset brings the timer back to the start");
    obs_properties_destroy(props);
    info->destroy(a);
    info->destroy(b);

    shim_set_int(d, "timer_mode", 0);
    shim_set_bool(d, "timer_buttons", 0);
    shim_set_string(d, "timer_label", "Stream time");
    a = info->create(d, NULL);
    props = info->get_properties(a);
    shim_click(props, "timer_start");
    frames(a, 4000, 1.0f);
    snap(a, "timer_up");
    obs_properties_destroy(props);
    info->destroy(a);
    shim_data_free(d);
}

static void test_now_playing(void)
{
    write_file("harness-np.txt", "Snowman\nlofi girl\n");
    obs_data_t *d = base(OV_NOW_PLAYING, 560, 140);
    shim_set_string(d, "np_file", "harness-np.txt");
    void *s = info->create(d, NULL);
    frames(s, 3, 1.0f / 30);
    uint32_t first = snap(s, "now_playing");
    write_file("harness-np.txt", "A really long song title that wont fit in the box at all\nsomeone with a long name too\n");
    frames(s, 31, 1.0f / 30);
    uint32_t second = snap(s, "now_playing_scroll");
    expect(first != second, "now playing picked up the new song from the file");
    write_file("harness-np.txt", "");
    frames(s, 31, 1.0f / 30);
    snap(s, "now_playing_empty");
    info->destroy(s);
    shim_data_free(d);
}

static void test_goal(void)
{
    obs_data_t *d = base(OV_GOAL, 560, 130);
    shim_set_double(d, "goal_current", 37);
    shim_set_double(d, "goal_target", 50);
    void *s = info->create(d, NULL);
    frames(s, 3, 1.0f / 60);
    snap(s, "goal");
    info->destroy(s);
    write_file("harness-goal.txt", "120 / 200\n");
    shim_set_int(d, "goal_source", 1);
    shim_set_string(d, "goal_file", "harness-goal.txt");
    shim_set_string(d, "goal_label", "Follower goal");
    s = info->create(d, NULL);
    frames(s, 3, 1.0f / 60);
    snap(s, "goal_file");
    info->destroy(s);
    shim_data_free(d);
}

static void test_clock(void)
{
    obs_data_t *d = base(OV_CLOCK, 360, 190);
    void *s = info->create(d, NULL);
    frames(s, 3, 1.0f / 60);
    snap(s, "clock");
    info->destroy(s);
    shim_set_bool(d, "clock_24h", 0);
    shim_set_bool(d, "clock_seconds", 0);
    shim_set_string(d, "clock_label", "my time");
    shim_set_int(d, "theme", GOOME_PRESET_PAPER);
    s = info->create(d, NULL);
    frames(s, 3, 1.0f / 60);
    snap(s, "clock_12h");
    info->destroy(s);
    shim_data_free(d);
}

static void test_ticker(void)
{
    write_file("harness-ticker.txt", "sev: hi chat\ngoome_fan: wait the overlay is clickable??\nmodbot: be nice in here\nsev: yes it is, its goome\nviewer42: first time here, love the colors\n");
    obs_data_t *d = base(OV_TICKER, 720, 74);
    shim_set_string(d, "ticker_file", "harness-ticker.txt");
    shim_set_string(d, "ticker_label", "CHAT");
    shim_set_int(d, "theme", GOOME_PRESET_MINT);
    void *s = info->create(d, NULL);
    frames(s, 60, 1.0f / 30);
    snap(s, "ticker");
    info->destroy(s);
    shim_data_free(d);
    d = base(OV_TICKER, 440, 230);
    shim_set_string(d, "ticker_file", "harness-ticker.txt");
    shim_set_int(d, "ticker_style", 1);
    shim_set_int(d, "theme", GOOME_PRESET_ROSE);
    s = info->create(d, NULL);
    frames(s, 3, 1.0f / 30);
    snap(s, "ticker_chat");
    info->destroy(s);
    shim_data_free(d);
}

static void test_builder(void)
{
    char path[1200];
    snprintf(path, sizeof(path), "%s/examples/stream.goome.json", root);
    obs_data_t *d = base(OV_BUILDER, 720, 440);
    shim_set_string(d, "builder_file", path);
    shim_set_int(d, "theme", -1);
    void *s = info->create(d, NULL);
    frames(s, 3, 1.0f / 60);
    gsrc *g = s;
    expect(strstr(ov_status(g->o), "loaded") != NULL, "the builder file loads");
    uint32_t first = snap(s, "builder");
    click(s, 63, 334);
    leave(s);
    uint32_t hyped = snap(s, "builder_hype");
    expect(first != hyped, "clicking Hype! in the builder file changes the ui");
    click(s, 530, 205);
    type(s, "sev rocks");
    leave(s);
    snap(s, "builder_typed");
    info->destroy(s);
    shim_set_int(d, "theme", GOOME_PRESET_LIGHT);
    s = info->create(d, NULL);
    frames(s, 3, 1.0f / 60);
    snap(s, "builder_light");
    info->destroy(s);
    shim_set_string(d, "builder_file", "harness-missing.goome.json");
    s = info->create(d, NULL);
    frames(s, 3, 1.0f / 60);
    snap(s, "builder_missing");
    info->destroy(s);
    shim_data_free(d);
}

static void test_props(void)
{
    obs_data_t *d = base(OV_GOAL, 400, 120);
    void *s = info->create(d, NULL);
    obs_properties_t *props = info->get_properties(s);
    shim_modified(props, "kind", d);
    expect(shim_visible(props, "goal_group") == 1 && shim_visible(props, "timer_group") == 0, "only the goal settings show for a goal bar");
    expect(shim_visible(props, "goal_file") == 0, "the goal file box hides while the goal comes from the settings");
    shim_set_int(d, "kind", OV_CLOCK);
    info->update(s, d);
    frames(s, 2, 1.0f / 60);
    shim_modified(props, "kind", d);
    expect(shim_visible(props, "clock_group") == 1 && shim_visible(props, "goal_group") == 0, "switching to a clock shows the clock settings");
    shim_set_int(d, "width", 300);
    shim_set_int(d, "height", 150);
    info->update(s, d);
    frames(s, 2, 1.0f / 60);
    expect(info->get_width(s) == 300 && info->get_height(s) == 150, "resizing in the settings resizes the source");
    snap(s, "resized");
    obs_properties_destroy(props);
    info->destroy(s);
    shim_data_free(d);
}

static int check(const char *file)
{
    FILE *f = fopen(file, "r");
    if (!f) {
        printf("FAIL cant read %s\n", file);
        return 1;
    }
    char name[128];
    unsigned hash;
    int bad = 0, seen = 0;
    while (fscanf(f, "%127s %x", name, &hash) == 2) {
        int found = 0;
        for (int i = 0; i < nresults; i++)
            if (!strcmp(results[i].name, name)) {
                found = 1, seen++;
                if (results[i].hash != hash) printf("FAIL %s hash %08x, expected %08x\n", name, results[i].hash, hash), bad++;
            }
        if (!found) printf("FAIL %s never got rendered\n", name), bad++;
    }
    fclose(f);
    if (seen != nresults) printf("FAIL %d renders arent in %s\n", nresults - seen, file), bad++;
    return bad;
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "print";
    if (argc > 2) root = argv[2];
    shots = !strcmp(mode, "shots");
    ov_now = fixed_now;
    if (!obs_module_load() || !(info = shim_source())) {
        printf("FAIL the module didnt register a source\n");
        return 1;
    }
    expect(!strcmp(info->id, "goome_overlay"), "the source id is goome_overlay");
    expect((info->output_flags & OBS_SOURCE_INTERACTION) != 0, "the source takes interaction");
    test_timer();
    test_now_playing();
    test_goal();
    test_clock();
    test_ticker();
    test_builder();
    test_props();
    obs_module_unload();
    remove("harness-np.txt");
    remove("harness-goal.txt");
    remove("harness-ticker.txt");
    if (!strcmp(mode, "check")) {
        char file[1200];
        snprintf(file, sizeof(file), "%s/tests/hashes.txt", root);
        failures += check(file);
    } else
        for (int i = 0; i < nresults; i++) printf("%s %08x\n", results[i].name, results[i].hash);
    printf("%d renders, %d uploads, %s\n", nresults, shim_uploads, failures ? "FAILED" : "all good");
    return failures != 0;
}
