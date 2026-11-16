#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MODULE_EXPORT
#define LOG_ERROR 100
#define LOG_WARNING 200
#define LOG_INFO 300
#define LOG_DEBUG 400

typedef struct obs_source obs_source_t;
typedef struct obs_data obs_data_t;
typedef struct obs_properties obs_properties_t;
typedef struct obs_property obs_property_t;
typedef struct gs_texture gs_texture_t;
typedef struct gs_effect gs_effect_t;
typedef struct gs_effect_param gs_eparam_t;
typedef struct obs_module obs_module_t;

void *bzalloc(size_t size);
void bfree(void *ptr);

const char *obs_module_text(const char *lookup);
#define OBS_DECLARE_MODULE() int shim_module_declared;
#define OBS_MODULE_USE_DEFAULT_LOCALE(name, locale) int shim_locale_declared;

long long obs_data_get_int(obs_data_t *data, const char *name);
double obs_data_get_double(obs_data_t *data, const char *name);
bool obs_data_get_bool(obs_data_t *data, const char *name);
const char *obs_data_get_string(obs_data_t *data, const char *name);
void obs_data_set_default_int(obs_data_t *data, const char *name, long long val);
void obs_data_set_default_double(obs_data_t *data, const char *name, double val);
void obs_data_set_default_bool(obs_data_t *data, const char *name, bool val);
void obs_data_set_default_string(obs_data_t *data, const char *name, const char *val);

enum obs_combo_type { OBS_COMBO_TYPE_INVALID, OBS_COMBO_TYPE_EDITABLE, OBS_COMBO_TYPE_LIST, OBS_COMBO_TYPE_RADIO };
enum obs_combo_format { OBS_COMBO_FORMAT_INVALID, OBS_COMBO_FORMAT_INT, OBS_COMBO_FORMAT_FLOAT, OBS_COMBO_FORMAT_STRING, OBS_COMBO_FORMAT_BOOL };
enum obs_path_type { OBS_PATH_FILE, OBS_PATH_FILE_SAVE, OBS_PATH_DIRECTORY };
enum obs_text_type { OBS_TEXT_DEFAULT, OBS_TEXT_PASSWORD, OBS_TEXT_MULTILINE, OBS_TEXT_INFO };
enum obs_group_type { OBS_COMBO_INVALID, OBS_GROUP_NORMAL, OBS_GROUP_CHECKABLE };

typedef bool (*obs_property_clicked_t)(obs_properties_t *props, obs_property_t *property, void *data);
typedef bool (*obs_property_modified_t)(obs_properties_t *props, obs_property_t *property, obs_data_t *settings);

obs_properties_t *obs_properties_create(void);
void obs_properties_destroy(obs_properties_t *props);
obs_property_t *obs_properties_get(obs_properties_t *props, const char *property);
obs_property_t *obs_properties_add_bool(obs_properties_t *props, const char *name, const char *description);
obs_property_t *obs_properties_add_int(obs_properties_t *props, const char *name, const char *description, int min, int max, int step);
obs_property_t *obs_properties_add_float(obs_properties_t *props, const char *name, const char *description, double min, double max, double step);
obs_property_t *obs_properties_add_float_slider(obs_properties_t *props, const char *name, const char *description, double min, double max, double step);
obs_property_t *obs_properties_add_text(obs_properties_t *props, const char *name, const char *description, enum obs_text_type type);
obs_property_t *obs_properties_add_path(obs_properties_t *props, const char *name, const char *description, enum obs_path_type type, const char *filter, const char *default_path);
obs_property_t *obs_properties_add_list(obs_properties_t *props, const char *name, const char *description, enum obs_combo_type type, enum obs_combo_format format);
obs_property_t *obs_properties_add_color_alpha(obs_properties_t *props, const char *name, const char *description);
obs_property_t *obs_properties_add_button2(obs_properties_t *props, const char *name, const char *text, obs_property_clicked_t callback, void *priv);
obs_property_t *obs_properties_add_group(obs_properties_t *props, const char *name, const char *description, enum obs_group_type type, obs_properties_t *group);
size_t obs_property_list_add_int(obs_property_t *p, const char *name, long long val);
void obs_property_set_modified_callback(obs_property_t *p, obs_property_modified_t modified);
void obs_property_set_visible(obs_property_t *p, bool visible);
const char *obs_property_name(obs_property_t *p);

enum obs_interaction_flags {
    INTERACT_NONE = 0,
    INTERACT_CAPS_KEY = 1,
    INTERACT_SHIFT_KEY = 1 << 1,
    INTERACT_CONTROL_KEY = 1 << 2,
    INTERACT_ALT_KEY = 1 << 3,
    INTERACT_MOUSE_LEFT = 1 << 4,
    INTERACT_MOUSE_MIDDLE = 1 << 5,
    INTERACT_MOUSE_RIGHT = 1 << 6,
    INTERACT_COMMAND_KEY = 1 << 7,
};

enum obs_mouse_button_type { MOUSE_LEFT, MOUSE_MIDDLE, MOUSE_RIGHT };

struct obs_mouse_event {
    uint32_t modifiers;
    int32_t x;
    int32_t y;
};

struct obs_key_event {
    uint32_t modifiers;
    char *text;
    uint32_t native_modifiers;
    uint32_t native_scancode;
    uint32_t native_vkey;
};

enum obs_source_type { OBS_SOURCE_TYPE_INPUT, OBS_SOURCE_TYPE_FILTER, OBS_SOURCE_TYPE_TRANSITION, OBS_SOURCE_TYPE_SCENE };
enum obs_icon_type { OBS_ICON_TYPE_UNKNOWN, OBS_ICON_TYPE_TEXT = 10 };

#define OBS_SOURCE_VIDEO (1 << 0)
#define OBS_SOURCE_CUSTOM_DRAW (1 << 3)
#define OBS_SOURCE_INTERACTION (1 << 5)

struct obs_source_info {
    const char *id;
    enum obs_source_type type;
    uint32_t output_flags;
    const char *(*get_name)(void *type_data);
    void *(*create)(obs_data_t *settings, obs_source_t *source);
    void (*destroy)(void *data);
    uint32_t (*get_width)(void *data);
    uint32_t (*get_height)(void *data);
    void (*get_defaults)(obs_data_t *settings);
    obs_properties_t *(*get_properties)(void *data);
    void (*update)(void *data, obs_data_t *settings);
    void (*video_tick)(void *data, float seconds);
    void (*video_render)(void *data, gs_effect_t *effect);
    void (*mouse_click)(void *data, const struct obs_mouse_event *event, int32_t type, bool mouse_up, uint32_t click_count);
    void (*mouse_move)(void *data, const struct obs_mouse_event *event, bool mouse_leave);
    void (*mouse_wheel)(void *data, const struct obs_mouse_event *event, int x_delta, int y_delta);
    void (*focus)(void *data, bool focus);
    void (*key_click)(void *data, const struct obs_key_event *event, bool key_up);
    enum obs_icon_type icon_type;
};

void obs_register_source_s(const struct obs_source_info *info, size_t size);
#define obs_register_source(info) obs_register_source_s(info, sizeof(struct obs_source_info))

enum gs_color_format { GS_UNKNOWN, GS_A8, GS_R8, GS_RGBA, GS_BGRX, GS_BGRA };
enum gs_blend_type { GS_BLEND_ZERO, GS_BLEND_ONE, GS_BLEND_SRCCOLOR, GS_BLEND_INVSRCCOLOR, GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA };
#define GS_DYNAMIC (1 << 1)

gs_texture_t *gs_texture_create(uint32_t width, uint32_t height, enum gs_color_format color_format, uint32_t levels, const uint8_t **data, uint32_t flags);
void gs_texture_destroy(gs_texture_t *tex);
void gs_texture_set_image(gs_texture_t *tex, const uint8_t *data, uint32_t linesize, bool invert);
void gs_blend_state_push(void);
void gs_blend_state_pop(void);
void gs_blend_function(enum gs_blend_type src, enum gs_blend_type dest);
bool gs_framebuffer_srgb_enabled(void);
void gs_enable_framebuffer_srgb(bool enable);
gs_eparam_t *gs_effect_get_param_by_name(const gs_effect_t *effect, const char *name);
void gs_effect_set_texture(gs_eparam_t *param, gs_texture_t *val);
bool gs_effect_loop(gs_effect_t *effect, const char *name);

enum obs_base_effect { OBS_EFFECT_DEFAULT, OBS_EFFECT_PREMULTIPLIED_ALPHA = 13 };
gs_effect_t *obs_get_base_effect(enum obs_base_effect effect);
void obs_source_draw(gs_texture_t *image, int x, int y, uint32_t cx, uint32_t cy, bool flip);
void obs_enter_graphics(void);
void obs_leave_graphics(void);

bool obs_module_load(void);
void obs_module_unload(void);
