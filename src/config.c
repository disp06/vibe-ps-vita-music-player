#include "config.h"
#include <stdio.h>
#include "i18n.h"
#include <stdlib.h>
#include <string.h>

Config cfg;

void config_defaults(void) {
    memset(&cfg, 0, sizeof cfg);
    cfg.skin = SKIN_METRO;
    cfg.dark = 1;
    cfg.accent = 0;
    cfg.screen_off = 10;
    cfg.pixel_shift = 1;
    cfg.repeat = 2;
    cfg.lang = LANG_EN;
}

#define INT_KEYS(X) X(skin) X(dark) X(accent) X(orient) X(screen_off) X(always_on) \
    X(pixel_shift) X(fps60) X(shuffle) X(repeat) X(lang)

void config_load(const char *path) {
    config_defaults();
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[600];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *v = eq + 1;
        v[strcspn(v, "\r\n")] = 0;
#define RD(k) if (!strcmp(line, #k)) cfg.k = atoi(v);
        INT_KEYS(RD)
        if (!strcmp(line, "last_dir")) snprintf(cfg.last_dir, sizeof cfg.last_dir, "%s", v);
    }
    fclose(f);
    if (cfg.skin < 0 || cfg.skin >= SKIN_COUNT) cfg.skin = 0;
    if (cfg.orient < 0 || cfg.orient > 2) cfg.orient = 0;
    if (cfg.screen_off != 0 && cfg.screen_off != 5 && cfg.screen_off != 10 && cfg.screen_off != 15) cfg.screen_off = 10;
    if (cfg.lang < 0 || cfg.lang >= LANG_COUNT) cfg.lang = LANG_EN;
}

void config_save(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return;
#define WR(k) fprintf(f, #k "=%d\n", cfg.k);
    INT_KEYS(WR)
    fprintf(f, "last_dir=%s\n", cfg.last_dir);
    fclose(f);
}
