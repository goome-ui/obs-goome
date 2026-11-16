#include <obs-module.h>
#include <plugin-support.h>

#include "goome-source.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
    return obs_module_text("Description");
}

bool obs_module_load(void)
{
    goome_overlay_register();
    obs_log(LOG_INFO, "goome overlay loaded (version %s)", PLUGIN_VERSION);
    return true;
}

void obs_module_unload(void)
{
    obs_log(LOG_INFO, "goome overlay unloaded");
}
