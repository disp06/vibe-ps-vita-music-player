#ifndef VIBE_UI_H
#define VIBE_UI_H
#include "browser.h"
#include "config.h"
#include "font.h"
#include "gfx.h"
#include "player.h"

enum { PAGE_PLAYING = 0, PAGE_LIBRARY = 1, PAGE_SETTINGS = 2, PAGE_COUNT };

enum {
    ACT_NONE, ACT_PLAYPAUSE, ACT_NEXT, ACT_PREV, ACT_STOP, ACT_SHUFFLE, ACT_REPEAT,
    ACT_ALWAYSON, ACT_SCREENOFF, ACT_ROTATE, ACT_THEME, ACT_PAGE, ACT_ROW, ACT_SETTING,
    ACT_UP, ACT_SEEKBAR, ACT_LIST, ACT_MORE
};

typedef struct { float x, y, w, h; int act, param; } Hit;

#define NBANDS 24
#define NWAVE 256
#define NSET 10

typedef struct UI {
    int W, H, land;
    double t;
    float dt;
    int page, prev_page;
    float page_t;      /* 0..1 transition progress */
    int page_dir;      /* +1 moved right, -1 left */
    PlayerState ps;
    float pos_smooth;
    int track_seen;
    double track_t;    /* time the current track appeared */
    Browser br;
    int sel;
    float scroll, scroll_v;
    float list_x, list_y, list_w, list_h, row_h;
    double list_t;     /* time the folder was opened (entry animation) */
    int set_sel;
    float set_scroll;
    float bands[NBANDS], peaks[NBANDS], wave[NWAVE], lvl_l, lvl_r;
    GTex *cover;
    int cover_id;
    Hit hits[96];
    int nhits, hits_on;
    int seeking;
    float seek_frac;
    float seek_x, seek_w, seek_y;
    char toast[96];
    double toast_t;
    int skin_state[8];
    double last_input;
} UI;

typedef struct {
    const char *name;
    void (*init)(UI *ui);
    void (*draw)(UI *ui);
    void (*reset)(UI *ui); /* theme / orientation changed */
} Skin;

extern const Skin skin_metro, skin_retro, skin_minimal;
extern UI ui;

/* helpers shared by skins */
void ui_hit(float x, float y, float w, float h, int act, int param);
void ui_list_area(float x, float y, float w, float h, float row_h);
void ui_seek_area(float x, float y, float w, float h);
void fmt_time(double s, char *buf);
float ui_ease(float t);      /* ease-out cubic */
float ui_clamp(float v, float a, float b);
float ui_progress(void);     /* playing position 0..1 (respects seek drag) */
double ui_pos(void);
const char *ui_title(void);
const char *ui_artist(void);
int ui_playing(void);

/* settings rows */
const char *set_label(int i);
void set_value(int i, char *buf, int max);
int set_is_toggle(int i);
int set_toggle_on(int i);

/* simple vector icons, (cx,cy) centre, s = half size */
void icon_play(float cx, float cy, float s, uint32_t c);
void icon_pause(float cx, float cy, float s, uint32_t c);
void icon_prev(float cx, float cy, float s, uint32_t c);
void icon_next(float cx, float cy, float s, uint32_t c);
void icon_stop(float cx, float cy, float s, uint32_t c);
void icon_shuffle(float cx, float cy, float s, float th, uint32_t c);
void icon_repeat(float cx, float cy, float s, float th, int one, uint32_t c);
void icon_bulb(float cx, float cy, float s, float th, int on, uint32_t c);
void icon_moon(float cx, float cy, float s, uint32_t c, uint32_t bg);
void icon_rotate(float cx, float cy, float s, float th, uint32_t c);
void icon_folder(float cx, float cy, float s, uint32_t c);
void icon_note(float cx, float cy, float s, uint32_t c);
void icon_dots(float cx, float cy, float s, uint32_t c);
void icon_gear(float cx, float cy, float s, float th, uint32_t c, uint32_t bg);
void icon_battery(float x, float y, float w, float h, int pct, uint32_t c);

#endif
