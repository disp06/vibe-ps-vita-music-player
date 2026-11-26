#ifndef VIBE_CONFIG_H
#define VIBE_CONFIG_H

enum { SKIN_METRO = 0, SKIN_RETRO = 1, SKIN_MINIMAL = 2, SKIN_COUNT };

typedef struct {
    int skin;
    int dark;          /* 1 = dark (true black), 0 = light */
    int accent;        /* index into the skin's accent table */
    int orient;        /* ORIENT_* */
    int screen_off;    /* seconds of inactivity before the display turns off, 0 = never */
    int always_on;
    int pixel_shift;   /* OLED burn-in protection while always-on */
    int fps60;
    int shuffle, repeat;
    int lang;          /* LANG_* from i18n.h, default English */
    char last_dir[512];
} Config;

extern Config cfg;
void config_defaults(void);
void config_load(const char *path);
void config_save(const char *path);

#endif
