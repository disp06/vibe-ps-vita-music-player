/* Desktop preview: renders every skin/page/orientation to PNG with fake player
 * data, so layouts can be reviewed without a Vita. Build: make -C tools/preview */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/app.h"
#include "../../src/sys.h"
#include "../../src/ui.h"

extern const char *gfx_host_out;
void app_test_set_skin(void);

/* ---- fake player ---- */
static PlayerState fake;
static double fake_t;
static uint8_t *fake_cover;
static int fake_cover_n, fake_cover_given;
int player_init(void) { return 0; }
void player_shutdown(void) {}
void player_play_list(char **p, int n, int s) {}
void player_toggle(void) {}
void player_stop(void) {}
void player_next(void) {}
void player_prev(void) {}
void player_seek(double s) {}
void player_set_shuffle(int on) { fake.shuffle = on; }
void player_set_repeat(int m) { fake.repeat = m; }
void player_get(PlayerState *st) { *st = fake; }
int player_take_cover(uint8_t **d, int *n, int *id) {
    if (fake_cover_given) return 0;
    fake_cover_given = 1;
    *d = NULL; *n = 0; *id = 1;
    if (fake_cover) { *d = malloc(fake_cover_n); memcpy(*d, fake_cover, fake_cover_n); *n = fake_cover_n; }
    return 1;
}
void player_viz(float *out, int n, float *l, float *r) {
    for (int i = 0; i < n; i++) {
        double t = fake_t + i / 22050.0;
        out[i] = 0.45 * sin(t * 2 * M_PI * 110) + 0.25 * sin(t * 2 * M_PI * 440 + 1) + 0.12 * sin(t * 2 * M_PI * 1800) +
                 0.06 * sin(t * 2 * M_PI * 5200) + 0.08 * ((rand() % 2000) / 1000.0 - 1);
    }
    *l = 0.72f; *r = 0.61f;
}

/* ---- fake sys ---- */
int sys_init(void) { return 0; }
void sys_exit(void) {}
void sys_tick(int a, int b, int c) {}
int sys_quit_requested(void) { return 0; }
int sys_visible(void) { return 1; }
int sys_foreground(void) { return 1; }
void sys_screen(int on) {}
int sys_screen_on(void) { return 1; }
void sys_perf(int l) {}
int sys_battery(void) { return 76; }
int sys_charging(void) { return 0; }
void sys_clock(int *h, int *m) { *h = 15; *m = 15; }
int sys_enter_is_cross(void) { return 1; }
double sys_time(void) { return fake_t; }

static const char *k_files[] = {"Airwave Sessions/", "Night Drive/", "01 - Neon Horizon.mp3", "02 - Static Bloom.flac",
                                "03 - Paper Satellites.ogg", "04 - Квартал огней.mp3", "05 - Glass Tide.wav",
                                "06 - Low Orbit Lullaby.mp3", "07 - Afterimage.flac", "08 - Tape Ghosts.mp3"};

static void render(const char *name, int skin, int page, int orient, int dark, double t) {
    cfg.skin = skin;
    cfg.dark = dark;
        app_test_set_skin();
    app_set_orientation(orient);
    ui.page = ui.prev_page = page;
    ui.page_t = 1;
    ui.list_t = -10;
    ui.track_t = -10;
    ui.sel = 3;
    ui.toast_t = -10;
    fake_t = t;
    for (int i = 0; i < 40; i++) app_update(1 / 30.0f, t - 1.3 + i / 30.0);
    fake.pos = 128.4 + t;
    ui.pos_smooth = fake.pos;
    gfx_host_out = name;
    gfx_begin(0xFF000000);
    app_draw();
    gfx_end();
    printf("%s\n", name);
}

int main(int argc, char **argv) {
    const char *outdir = argc > 1 ? argv[1] : ".";
    FILE *f = fopen("tools/preview/cover.png", "rb");
    if (f) {
        fseek(f, 0, SEEK_END); fake_cover_n = ftell(f); fseek(f, 0, SEEK_SET);
        fake_cover = malloc(fake_cover_n); fread(fake_cover, 1, fake_cover_n, f); fclose(f);
    }
    gfx_init();
    font_init("assets/fonts");
    fake.status = PS_PLAYING;
    fake.track_id = 1;
    strcpy(fake.title, "Neon Horizon (In the Air Dub)");
    strcpy(fake.artist, "Kinetic Avenue");
    strcpy(fake.album, "Airwave Sessions");
    strcpy(fake.next_title, "02 - Static Bloom");
    strcpy(fake.path, "ux0:/music/Airwave Sessions/01 - Neon Horizon.mp3");
    fake.codec = "MP3";
    fake.rate = 44100; fake.channels = 2; fake.bits = 16; fake.bitrate = 320;
    fake.dur = 326.0; fake.repeat = 2; fake.qcount = 8; fake.qindex = 0;
    app_init("/tmp");
    cfg.repeat = 2;
    browser_fake(&ui.br, "ux0:/music/Airwave Sessions", k_files, sizeof k_files / sizeof *k_files);
    static const char *skins[] = {"metro", "retro", "minimal"};
    static const char *pages[] = {"playing", "library", "settings"};
    char name[512];
    int only = argc > 2 ? atoi(argv[2]) : -1;
    for (int s = 0; s < SKIN_COUNT; s++) {
        if (only >= 0 && s != only) continue;
        for (int o = 0; o < 2; o++)
            for (int p = 0; p < 3; p++) {
                snprintf(name, sizeof name, "%s/%s_%s_%s_dark.png", outdir, skins[s], o ? "portrait" : "landscape", pages[p]);
                render(name, s, p, o, 1, 3.0);
            }
        /* mid page-transition frame */
        cfg.skin = s; app_test_set_skin(); app_set_orientation(0);
        ui.prev_page = PAGE_LIBRARY; ui.page = PAGE_PLAYING; ui.page_dir = -1; ui.page_t = 0.45f;
        snprintf(name, sizeof name, "%s/%s_transition.png", outdir, skins[s]);
        gfx_host_out = name; gfx_begin(0xFF000000); app_draw(); gfx_end();
        snprintf(name, sizeof name, "%s/%s_landscape_playing_light.png", outdir, skins[s]);
        render(name, s, 0, 0, 0, 3.0);
        snprintf(name, sizeof name, "%s/%s_portrait_library_light.png", outdir, skins[s]);
        render(name, s, 1, 1, 0, 3.0);
    }
    return 0;
}
