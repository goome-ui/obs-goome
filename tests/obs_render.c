#include <obs.h>
#include <obs-nix-platform.h>
#include <util/platform.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *XOpenDisplay(const char *name);

static uint32_t fnv(const uint8_t *px, uint32_t ls, uint32_t w, uint32_t h)
{
    uint32_t v = 2166136261u;
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w * 4; x++) v = (v ^ px[y * ls + x]) * 16777619u;
    return v;
}

static uint32_t grab(obs_source_t *s, const char *path, long *alpha)
{
    uint32_t w = obs_source_get_width(s), h = obs_source_get_height(s), hash = 0;
    *alpha = 0;
    obs_enter_graphics();
    gs_texrender_t *tr = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    gs_stagesurf_t *st = gs_stagesurface_create(w, h, GS_RGBA);
    if (gs_texrender_begin(tr, w, h)) {
        struct vec4 clear;
        vec4_zero(&clear);
        gs_clear(GS_CLEAR_COLOR, &clear, 0, 0);
        gs_ortho(0, (float)w, 0, (float)h, -100, 100);
        obs_source_video_render(s);
        gs_texrender_end(tr);
    }
    gs_stage_texture(st, gs_texrender_get_texture(tr));
    uint8_t *px;
    uint32_t ls;
    if (gs_stagesurface_map(st, &px, &ls)) {
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++) *alpha += px[y * ls + x * 4 + 3];
        hash = fnv(px, ls, w, h);
        FILE *f = path ? fopen(path, "wb") : NULL;
        if (f) {
            fprintf(f, "P6\n%u %u\n255\n", w, h);
            for (uint32_t y = 0; y < h; y++)
                for (uint32_t x = 0; x < w; x++) {
                    const uint8_t *p = px + y * ls + x * 4;
                    unsigned char o[3] = {p[0], p[1], p[2]};
                    for (int k = 0; k < 3; k++) o[k] = (unsigned char)(o[k] + 0x30 * (255 - p[3]) / 255);
                    fwrite(o, 1, 3, f);
                }
            fclose(f);
        }
        gs_stagesurface_unmap(st);
    }
    gs_stagesurface_destroy(st);
    gs_texrender_destroy(tr);
    obs_leave_graphics();
    return hash;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "obs_render <obs-goome.so> <plugin data dir> <libobs data dir> [before.ppm after.ppm]\n");
        return 2;
    }
    obs_set_nix_platform(OBS_NIX_PLATFORM_X11_EGL);
    obs_set_nix_platform_display(XOpenDisplay(NULL));
    obs_add_data_path(argv[3]);
    if (!obs_startup("en-US", NULL, NULL)) {
        puts("FAIL obs_startup");
        return 1;
    }
    struct obs_video_info ovi;
    memset(&ovi, 0, sizeof(ovi));
    ovi.graphics_module = "libobs-opengl";
    ovi.fps_num = 60, ovi.fps_den = 1;
    ovi.base_width = ovi.output_width = 640, ovi.base_height = ovi.output_height = 360;
    ovi.output_format = VIDEO_FORMAT_RGBA;
    ovi.colorspace = VIDEO_CS_SRGB, ovi.range = VIDEO_RANGE_FULL;
    ovi.scale_type = OBS_SCALE_BICUBIC;
    if (obs_reset_video(&ovi) != OBS_VIDEO_SUCCESS) {
        puts("FAIL obs_reset_video");
        return 1;
    }
    obs_module_t *m = NULL;
    if (obs_open_module(&m, argv[1], argv[2]) != MODULE_SUCCESS || !obs_init_module(m)) {
        puts("FAIL the module didnt load");
        return 1;
    }
    obs_data_t *d = obs_data_create();
    obs_data_set_int(d, "kind", 1);
    obs_source_t *s = obs_source_create("goome_overlay", "goome", d, NULL);
    if (!s) {
        puts("FAIL no goome_overlay source");
        return 1;
    }
    obs_set_output_source(0, s);
    os_sleep_ms(1500);
    long a1, a2;
    uint32_t before = grab(s, argc > 4 ? argv[4] : NULL, &a1);
    struct obs_mouse_event me = {0, 126, 160};
    obs_source_send_mouse_move(s, &me, false);
    obs_source_send_mouse_click(s, &me, MOUSE_LEFT, false, 1);
    obs_source_send_mouse_click(s, &me, MOUSE_LEFT, true, 1);
    os_sleep_ms(2300);
    obs_source_send_mouse_move(s, &me, true);
    os_sleep_ms(200);
    uint32_t after = grab(s, argc > 5 ? argv[5] : NULL, &a2);
    printf("before %08x alpha %ld, after clicking Start %08x alpha %ld\n", before, a1, after, a2);
    int bad = !a1 || !a2 || before == after;
    obs_set_output_source(0, NULL);
    obs_source_release(s);
    obs_data_release(d);
    obs_shutdown();
    puts(bad ? "FAIL nothing drew or the click did nothing" : "real libobs drew it and the click started the timer");
    return bad;
}
