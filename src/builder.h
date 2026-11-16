#pragma once

#include "goome.h"

typedef struct bdoc bdoc;

bdoc *bdoc_load(const char *path, char *err, int errcap);
bdoc *bdoc_parse(const char *text, size_t len, const char *dir, char *err, int errcap);
void bdoc_free(bdoc *d);
void bdoc_theme(bdoc *d, goome_theme *t);
void bdoc_draw(bdoc *d, goome_ui *u, goome *g);
int bdoc_count(const bdoc *d);
int bdoc_skipped(const bdoc *d);

int ov_theme_field(const char *name, int *is_color);
void ov_theme_set(goome_theme *t, int field, double v);
