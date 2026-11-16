#pragma once

#include <stddef.h>

enum { JS_NULL, JS_BOOL, JS_NUM, JS_STR, JS_ARR, JS_OBJ };

typedef struct js js;
struct js {
    int type;
    double num;
    char *str;
    char *key;
    js *kids;
    int n;
};

js *js_parse(const char *text, size_t len, char *err, int errcap);
void js_free(js *v);
const js *js_get(const js *obj, const char *key);
const js *js_at(const js *arr, int i);
double js_num(const js *v, double def);
int js_bool(const js *v, int def);
const char *js_str(const js *v, const char *def);
