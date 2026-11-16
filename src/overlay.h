#pragma once

#include <stdio.h>
#include <time.h>

#include "goome.h"

enum { OV_BUILDER, OV_TIMER, OV_NOW_PLAYING, OV_GOAL, OV_CLOCK, OV_TICKER, OV_KIND_COUNT };
enum { OV_TIMER_START, OV_TIMER_PAUSE, OV_TIMER_TOGGLE, OV_TIMER_RESET };

typedef struct {
    int kind, width, height;
    int theme;
    int use_accent;
    uint32_t accent;
    double scale;
    char font[256];
    int animations, card;
    char builder_path[1024];
    int timer_down, timer_seconds, timer_buttons;
    char timer_label[128], timer_done[128];
    char np_path[1024], np_label[128], np_empty[128];
    char goal_label[128], goal_path[1024];
    double goal_current, goal_target;
    int goal_file;
    int clock_24h, clock_seconds, clock_date;
    char clock_label[128];
    char ticker_path[1024], ticker_label[128];
    int ticker_chat, ticker_lines;
    double ticker_speed;
} ov_settings;

typedef struct ov ov;

extern void (*ov_now)(struct tm *out);
extern FILE *(*ov_fopen)(const char *path, const char *mode);

void ov_defaults(ov_settings *s);
ov *ov_new(const ov_settings *s);
void ov_free(ov *o);
void ov_set(ov *o, const ov_settings *s);
void ov_push(ov *o, const goome_event *e);
void ov_timer(ov *o, int what);
int ov_tick(ov *o, double dt);
goome *ov_goome(ov *o);
const char *ov_status(const ov *o);
