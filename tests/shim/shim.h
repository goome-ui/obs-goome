#pragma once

#include <stdio.h>

#include "obs-module.h"

obs_data_t *shim_data_new(void);
void shim_data_free(obs_data_t *d);
void shim_set_int(obs_data_t *d, const char *name, long long v);
void shim_set_double(obs_data_t *d, const char *name, double v);
void shim_set_bool(obs_data_t *d, const char *name, int v);
void shim_set_string(obs_data_t *d, const char *name, const char *v);
int shim_click(obs_properties_t *props, const char *name);
int shim_modified(obs_properties_t *props, const char *name, obs_data_t *settings);
int shim_visible(obs_properties_t *props, const char *name);
const struct obs_source_info *shim_source(void);
const uint8_t *shim_texture(int *w, int *h);
extern int shim_uploads, shim_draws;
