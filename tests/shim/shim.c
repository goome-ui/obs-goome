#include "shim.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <plugin-support.h>

const char *PLUGIN_NAME = "obs-goome";
const char *PLUGIN_VERSION = "test";

enum { SV_NONE, SV_INT, SV_DOUBLE, SV_BOOL, SV_STRING };

typedef struct {
    char name[64];
    int type, def_type;
    long long i, def_i;
    double d, def_d;
    char s[1024], def_s[1024];
} sval;

struct obs_data {
    sval v[96];
    int n;
};

struct obs_property {
    char name[64];
    int visible, is_group;
    obs_property_clicked_t clicked;
    obs_property_modified_t modified;
    void *priv;
};

struct obs_properties {
    obs_property_t *p[160];
    int n;
};

struct gs_texture {
    uint32_t w, h;
    uint8_t *px;
};

static struct obs_source_info shim_info;
static int shim_registered;
static gs_texture_t *shim_last;
static int shim_logs_quiet = 1;
int shim_uploads, shim_draws;

void *bzalloc(size_t size)
{
    return calloc(1, size ? size : 1);
}

void bfree(void *ptr)
{
    free(ptr);
}

const char *obs_module_text(const char *lookup)
{
    return lookup;
}

void blogva(int log_level, const char *format, va_list args)
{
    if (shim_logs_quiet && log_level >= LOG_INFO) return;
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
}

void obs_log(int log_level, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    blogva(log_level, format, args);
    va_end(args);
}

FILE *os_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

obs_data_t *shim_data_new(void)
{
    return calloc(1, sizeof(obs_data_t));
}

void shim_data_free(obs_data_t *d)
{
    free(d);
}

static sval *slot(obs_data_t *d, const char *name, int make)
{
    for (int i = 0; i < d->n; i++)
        if (!strcmp(d->v[i].name, name)) return &d->v[i];
    if (!make || d->n >= (int)(sizeof(d->v) / sizeof(d->v[0]))) return NULL;
    sval *v = &d->v[d->n++];
    snprintf(v->name, sizeof(v->name), "%s", name);
    return v;
}

void shim_set_int(obs_data_t *d, const char *name, long long v)
{
    sval *s = slot(d, name, 1);
    if (s) s->type = SV_INT, s->i = v;
}

void shim_set_double(obs_data_t *d, const char *name, double v)
{
    sval *s = slot(d, name, 1);
    if (s) s->type = SV_DOUBLE, s->d = v;
}

void shim_set_bool(obs_data_t *d, const char *name, int v)
{
    sval *s = slot(d, name, 1);
    if (s) s->type = SV_BOOL, s->i = v != 0;
}

void shim_set_string(obs_data_t *d, const char *name, const char *v)
{
    sval *s = slot(d, name, 1);
    if (s) s->type = SV_STRING, snprintf(s->s, sizeof(s->s), "%s", v);
}

long long obs_data_get_int(obs_data_t *data, const char *name)
{
    sval *s = slot(data, name, 0);
    if (!s) return 0;
    if (s->type == SV_INT || s->type == SV_BOOL) return s->i;
    if (s->type == SV_DOUBLE) return (long long)s->d;
    if (s->def_type == SV_DOUBLE) return (long long)s->def_d;
    return s->def_i;
}

double obs_data_get_double(obs_data_t *data, const char *name)
{
    sval *s = slot(data, name, 0);
    if (!s) return 0;
    if (s->type == SV_DOUBLE) return s->d;
    if (s->type == SV_INT) return (double)s->i;
    if (s->def_type == SV_INT) return (double)s->def_i;
    return s->def_d;
}

bool obs_data_get_bool(obs_data_t *data, const char *name)
{
    sval *s = slot(data, name, 0);
    if (!s) return false;
    if (s->type == SV_BOOL || s->type == SV_INT) return s->i != 0;
    return s->def_i != 0;
}

const char *obs_data_get_string(obs_data_t *data, const char *name)
{
    sval *s = slot(data, name, 0);
    if (!s) return "";
    return s->type == SV_STRING ? s->s : s->def_s;
}

void obs_data_set_default_int(obs_data_t *data, const char *name, long long val)
{
    sval *s = slot(data, name, 1);
    if (s) s->def_type = SV_INT, s->def_i = val;
}

void obs_data_set_default_double(obs_data_t *data, const char *name, double val)
{
    sval *s = slot(data, name, 1);
    if (s) s->def_type = SV_DOUBLE, s->def_d = val;
}

void obs_data_set_default_bool(obs_data_t *data, const char *name, bool val)
{
    sval *s = slot(data, name, 1);
    if (s) s->def_type = SV_BOOL, s->def_i = val;
}

void obs_data_set_default_string(obs_data_t *data, const char *name, const char *val)
{
    sval *s = slot(data, name, 1);
    if (s) s->def_type = SV_STRING, snprintf(s->def_s, sizeof(s->def_s), "%s", val ? val : "");
}

obs_properties_t *obs_properties_create(void)
{
    return calloc(1, sizeof(obs_properties_t));
}

void obs_properties_destroy(obs_properties_t *props)
{
    if (!props) return;
    for (int i = 0; i < props->n; i++) free(props->p[i]);
    free(props);
}

obs_property_t *obs_properties_get(obs_properties_t *props, const char *property)
{
    for (int i = 0; props && i < props->n; i++)
        if (!strcmp(props->p[i]->name, property)) return props->p[i];
    return NULL;
}

static obs_property_t *add(obs_properties_t *props, const char *name)
{
    if (props->n >= (int)(sizeof(props->p) / sizeof(props->p[0]))) return NULL;
    obs_property_t *p = calloc(1, sizeof(obs_property_t));
    if (!p) return NULL;
    snprintf(p->name, sizeof(p->name), "%s", name);
    p->visible = 1;
    props->p[props->n++] = p;
    return p;
}

obs_property_t *obs_properties_add_bool(obs_properties_t *props, const char *name, const char *description)
{
    (void)description;
    return add(props, name);
}

obs_property_t *obs_properties_add_int(obs_properties_t *props, const char *name, const char *description, int min, int max, int step)
{
    (void)description, (void)min, (void)max, (void)step;
    return add(props, name);
}

obs_property_t *obs_properties_add_float(obs_properties_t *props, const char *name, const char *description, double min, double max, double step)
{
    (void)description, (void)min, (void)max, (void)step;
    return add(props, name);
}

obs_property_t *obs_properties_add_float_slider(obs_properties_t *props, const char *name, const char *description, double min, double max, double step)
{
    (void)description, (void)min, (void)max, (void)step;
    return add(props, name);
}

obs_property_t *obs_properties_add_text(obs_properties_t *props, const char *name, const char *description, enum obs_text_type type)
{
    (void)description, (void)type;
    return add(props, name);
}

obs_property_t *obs_properties_add_path(obs_properties_t *props, const char *name, const char *description, enum obs_path_type type, const char *filter, const char *default_path)
{
    (void)description, (void)type, (void)filter, (void)default_path;
    return add(props, name);
}

obs_property_t *obs_properties_add_list(obs_properties_t *props, const char *name, const char *description, enum obs_combo_type type, enum obs_combo_format format)
{
    (void)description, (void)type, (void)format;
    return add(props, name);
}

obs_property_t *obs_properties_add_color_alpha(obs_properties_t *props, const char *name, const char *description)
{
    (void)description;
    return add(props, name);
}

obs_property_t *obs_properties_add_button2(obs_properties_t *props, const char *name, const char *text, obs_property_clicked_t callback, void *priv)
{
    (void)text;
    obs_property_t *p = add(props, name);
    if (p) p->clicked = callback, p->priv = priv;
    return p;
}

obs_property_t *obs_properties_add_group(obs_properties_t *props, const char *name, const char *description, enum obs_group_type type, obs_properties_t *group)
{
    (void)description, (void)type;
    for (int i = 0; group && i < group->n && props->n < (int)(sizeof(props->p) / sizeof(props->p[0])); i++) props->p[props->n++] = group->p[i];
    if (group) group->n = 0;
    obs_properties_destroy(group);
    obs_property_t *p = add(props, name);
    if (p) p->is_group = 1;
    return p;
}

size_t obs_property_list_add_int(obs_property_t *p, const char *name, long long val)
{
    (void)p, (void)name, (void)val;
    return 0;
}

void obs_property_set_modified_callback(obs_property_t *p, obs_property_modified_t modified)
{
    if (p) p->modified = modified;
}

void obs_property_set_visible(obs_property_t *p, bool visible)
{
    if (p) p->visible = visible;
}

const char *obs_property_name(obs_property_t *p)
{
    return p ? p->name : "";
}

int shim_click(obs_properties_t *props, const char *name)
{
    obs_property_t *p = obs_properties_get(props, name);
    if (!p || !p->clicked) return 0;
    p->clicked(props, p, p->priv);
    return 1;
}

int shim_modified(obs_properties_t *props, const char *name, obs_data_t *settings)
{
    obs_property_t *p = obs_properties_get(props, name);
    if (!p || !p->modified) return 0;
    p->modified(props, p, settings);
    return 1;
}

int shim_visible(obs_properties_t *props, const char *name)
{
    obs_property_t *p = obs_properties_get(props, name);
    return p ? p->visible : -1;
}

void obs_register_source_s(const struct obs_source_info *info, size_t size)
{
    if (size != sizeof(shim_info)) return;
    shim_info = *info;
    shim_registered = 1;
}

const struct obs_source_info *shim_source(void)
{
    return shim_registered ? &shim_info : NULL;
}

gs_texture_t *gs_texture_create(uint32_t width, uint32_t height, enum gs_color_format color_format, uint32_t levels, const uint8_t **data, uint32_t flags)
{
    (void)levels, (void)data, (void)flags;
    if (color_format != GS_BGRA || !width || !height) return NULL;
    gs_texture_t *t = calloc(1, sizeof(gs_texture_t));
    if (!t) return NULL;
    t->w = width, t->h = height;
    t->px = calloc((size_t)width * height, 4);
    if (!t->px) {
        free(t);
        return NULL;
    }
    shim_last = t;
    return t;
}

void gs_texture_destroy(gs_texture_t *tex)
{
    if (!tex) return;
    if (shim_last == tex) shim_last = NULL;
    free(tex->px);
    free(tex);
}

void gs_texture_set_image(gs_texture_t *tex, const uint8_t *data, uint32_t linesize, bool invert)
{
    for (uint32_t y = 0; y < tex->h; y++) {
        uint32_t sy = invert ? tex->h - 1 - y : y;
        memcpy(tex->px + (size_t)y * tex->w * 4, data + (size_t)sy * linesize, (size_t)tex->w * 4);
    }
    shim_uploads++;
}

const uint8_t *shim_texture(int *w, int *h)
{
    if (!shim_last) return NULL;
    *w = (int)shim_last->w, *h = (int)shim_last->h;
    return shim_last->px;
}

void gs_blend_state_push(void)
{
}

void gs_blend_state_pop(void)
{
}

void gs_blend_function(enum gs_blend_type src, enum gs_blend_type dest)
{
    (void)src, (void)dest;
}

static bool srgb_on = true;

bool gs_framebuffer_srgb_enabled(void)
{
    return srgb_on;
}

void gs_enable_framebuffer_srgb(bool enable)
{
    srgb_on = enable;
}

static gs_effect_t *const premul_effect = (gs_effect_t *)&shim_info;
static int loop_left;

gs_eparam_t *gs_effect_get_param_by_name(const gs_effect_t *effect, const char *name)
{
    (void)effect, (void)name;
    loop_left = 1;
    return (gs_eparam_t *)&shim_info;
}

void gs_effect_set_texture(gs_eparam_t *param, gs_texture_t *val)
{
    (void)param, (void)val;
}

bool gs_effect_loop(gs_effect_t *effect, const char *name)
{
    (void)effect, (void)name;
    return loop_left-- > 0;
}

gs_effect_t *obs_get_base_effect(enum obs_base_effect effect)
{
    return effect == OBS_EFFECT_PREMULTIPLIED_ALPHA ? premul_effect : NULL;
}

void obs_source_draw(gs_texture_t *image, int x, int y, uint32_t cx, uint32_t cy, bool flip)
{
    (void)image, (void)x, (void)y, (void)cx, (void)cy, (void)flip;
    shim_draws++;
}

void obs_enter_graphics(void)
{
}

void obs_leave_graphics(void)
{
}
