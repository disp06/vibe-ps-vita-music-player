/* Platform-independent application logic: pages, input, settings, visualiser */
#include "app.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sys.h"
#include "ui.h"

static const Skin *g_skins[SKIN_COUNT] = {&skin_metro, &skin_retro, &skin_minimal};
static const Skin *g_skin;
static char g_cfg_path[512];

/* touch tracking */
static int t_active, t_moved, t_scrolling, t_hit;
static float t_x0, t_y0, t_x, t_y, t_scroll0, t_lasty;
static double t_time0;
/* d-pad auto repeat */
static double rep_next;
static unsigned rep_btn;

static const Skin *skin(void) { return g_skins[cfg.skin]; }

void app_toast(const char *msg) {
    snprintf(ui.toast, sizeof ui.toast, "%s", msg);
    ui.toast_t = ui.t;
}

static void apply_skin(void) {
    g_skin = skin();
    if (g_skin->init) g_skin->init(&ui);
}

void app_set_orientation(int o) {
    cfg.orient = o;
    gfx_set_orientation(o);
    ui.W = gfx_w();
    ui.H = gfx_h();
    ui.land = ui.W > ui.H;
    if (g_skin && g_skin->reset) g_skin->reset(&ui);
}

void app_screen_off(void) { sys_screen(0); }

void app_init(const char *data_dir) {
    snprintf(g_cfg_path, sizeof g_cfg_path, "%s/config.txt", data_dir);
    config_load(g_cfg_path);
    memset(&ui, 0, sizeof ui);
    ui.page = ui.prev_page = PAGE_LIBRARY;
    ui.page_t = 1;
    ui.cover_id = -1;
    ui.track_seen = -1;
    app_set_orientation(cfg.orient);
    apply_skin();
    player_set_shuffle(cfg.shuffle);
    player_set_repeat(cfg.repeat);
    if (!cfg.last_dir[0] || browser_open(&ui.br, cfg.last_dir) != 0) {
        if (browser_open(&ui.br, "ux0:/music") != 0 && browser_open(&ui.br, "ux0:/Music") != 0) browser_open(&ui.br, "");
    }
    ui.list_t = 0;
}

void app_shutdown(void) {
    snprintf(cfg.last_dir, sizeof cfg.last_dir, "%s", ui.br.path);
    config_save(g_cfg_path);
}

static void save(void) { config_save(g_cfg_path); }

static void goto_page(int p) {
    p = (p + PAGE_COUNT) % PAGE_COUNT;
    if (p == ui.page) return;
    int d = p - ui.page;
    if (d == 2) d = -1;
    if (d == -2) d = 1;
    ui.page_dir = d > 0 ? 1 : -1;
    ui.prev_page = ui.page;
    ui.page = p;
    ui.page_t = 0;
    if (p == PAGE_LIBRARY) ui.list_t = ui.t;
}

static void ensure_visible(void) {
    if (ui.row_h <= 0) return;
    float top = ui.sel * ui.row_h, bot = top + ui.row_h;
    if (top < ui.scroll) ui.scroll = top;
    if (bot > ui.scroll + ui.list_h) ui.scroll = bot - ui.list_h;
}

static void open_row(int i) {
    if (i < 0 || i >= ui.br.n) return;
    if (ui.br.e[i].is_dir) {
        if (browser_enter(&ui.br, i) != 0) {
            char m[96];
            snprintf(m, sizeof m, "can't open folder (0x%08X)", (unsigned)browser_last_error);
            app_toast(m);
            ui.sel = i;
            return;
        }
        ui.sel = 0;
        ui.scroll = 0;
        ui.list_t = ui.t;
        snprintf(cfg.last_dir, sizeof cfg.last_dir, "%s", ui.br.path);
        save();
        return;
    }
    char **paths;
    int start;
    int n = browser_playlist(&ui.br, i, &paths, &start);
    if (n > 0) {
        player_play_list(paths, n, start);
        goto_page(PAGE_PLAYING);
    }
    browser_free_list(paths, n);
}

static void go_up(void) {
    int i = browser_up(&ui.br);
    ui.sel = i;
    ui.scroll = 0;
    ui.list_t = ui.t;
    ensure_visible();
}

static void set_change(int i, int d) {
    switch (i) {
    case 0: cfg.skin = (cfg.skin + d + SKIN_COUNT) % SKIN_COUNT; apply_skin(); break;
    case 1: cfg.dark ^= 1; break;
    case 2: cfg.accent = (cfg.accent + d + 6) % 6; break;
    case 3: app_set_orientation((cfg.orient + d + 3) % 3); break;
    case 4: {
        static const int v[] = {0, 5, 10, 15};
        int k = 0;
        for (int j = 0; j < 4; j++) if (v[j] == cfg.screen_off) k = j;
        cfg.screen_off = v[(k + d + 4) % 4];
        break;
    }
    case 5: cfg.always_on ^= 1; break;
    case 6: cfg.pixel_shift ^= 1; break;
    case 7: cfg.fps60 ^= 1; break;
    default: return;
    }
    if (g_skin->reset) g_skin->reset(&ui);
    save();
}

static void do_action(int act, int param) {
    char msg[64];
    switch (act) {
    case ACT_PLAYPAUSE:
        if (ui.ps.qcount == 0) {
            /* nothing queued yet: play the folder from the selected file (or the first one) */
            int i = ui.sel;
            if (i >= ui.br.n || ui.br.e[i].is_dir)
                for (i = 0; i < ui.br.n && ui.br.e[i].is_dir; i++) {}
            if (i < ui.br.n) open_row(i);
            else goto_page(PAGE_LIBRARY);
        } else player_toggle();
        break;
    case ACT_NEXT: player_next(); break;
    case ACT_PREV: player_prev(); break;
    case ACT_STOP: player_stop(); break;
    case ACT_SHUFFLE:
        cfg.shuffle ^= 1;
        player_set_shuffle(cfg.shuffle);
        app_toast(cfg.shuffle ? "shuffle on" : "shuffle off");
        save();
        break;
    case ACT_REPEAT:
        cfg.repeat = (cfg.repeat + 1) % 3;
        player_set_repeat(cfg.repeat);
        app_toast(cfg.repeat == REPEAT_OFF ? "repeat off" : cfg.repeat == REPEAT_ONE ? "repeat track" : "repeat all");
        save();
        break;
    case ACT_ALWAYSON:
        cfg.always_on ^= 1;
        snprintf(msg, sizeof msg, "always on: %s", cfg.always_on ? "ON" : "off");
        app_toast(msg);
        save();
        break;
    case ACT_SCREENOFF: app_screen_off(); break;
    case ACT_ROTATE: set_change(3, 1); break;
    case ACT_THEME: set_change(1, 1); break;
    case ACT_PAGE: goto_page(param); break;
    case ACT_MORE: goto_page(PAGE_SETTINGS); break;
    case ACT_ROW: ui.sel = param; open_row(param); break;
    case ACT_SETTING: ui.set_sel = param; set_change(param, 1); break;
    case ACT_UP: go_up(); break;
    }
}

static Hit *hit_at(float x, float y) {
    for (int i = ui.nhits - 1; i >= 0; i--) {
        Hit *h = &ui.hits[i];
        if (x >= h->x && x < h->x + h->w && y >= h->y && y < h->y + h->h) return h;
    }
    return NULL;
}

static void touch(const Input *in) {
    if (in->touch && !t_active) {
        t_active = 1;
        t_moved = t_scrolling = 0;
        t_x0 = t_x = in->tx;
        t_y0 = t_y = t_lasty = in->ty;
        t_time0 = ui.t;
        Hit *h = hit_at(in->tx, in->ty);
        t_hit = h ? (int)(h - ui.hits) : -1;
        t_scroll0 = ui.page == PAGE_SETTINGS ? ui.set_scroll : ui.scroll;
        ui.scroll_v = 0;
        if (h && h->act == ACT_SEEKBAR && ui.ps.dur > 0) {
            ui.seeking = 1;
            ui.seek_frac = ui_clamp((in->tx - ui.seek_x) / ui.seek_w, 0, 1);
        }
        return;
    }
    if (in->touch && t_active) {
        t_x = in->tx;
        t_y = in->ty;
        float dx = t_x - t_x0, dy = t_y - t_y0;
        if (fabsf(dx) > 12 || fabsf(dy) > 12) t_moved = 1;
        if (ui.seeking) {
            ui.seek_frac = ui_clamp((t_x - ui.seek_x) / ui.seek_w, 0, 1);
        } else if (t_hit >= 0 && ui.hits[t_hit].act == ACT_LIST && fabsf(dy) > 10 && fabsf(dy) > fabsf(dx)) {
            t_scrolling = 1;
            if (ui.page == PAGE_SETTINGS) ui.set_scroll = t_scroll0 - dy;
            else ui.scroll = t_scroll0 - dy;
            ui.scroll_v = (t_lasty - t_y) / (ui.dt > 0 ? ui.dt : 0.016f);
        }
        t_lasty = t_y;
        return;
    }
    if (!in->touch && t_active) {
        t_active = 0;
        float dx = t_x - t_x0, dy = t_y - t_y0;
        if (ui.seeking) {
            ui.seeking = 0;
            double target = ui.seek_frac * ui.ps.dur;
            player_seek(target);
            ui.pos_smooth = target;
            return;
        }
        if (t_scrolling) return; /* momentum continues in update */
        if (fabsf(dx) > 70 && fabsf(dx) > 2 * fabsf(dy) && ui.t - t_time0 < 0.8) {
            goto_page(ui.page + (dx < 0 ? 1 : -1));
            return;
        }
        if (t_moved || t_hit < 0) return;
        Hit *h = &ui.hits[t_hit];
        if (h->act == ACT_LIST) {
            if (ui.page == PAGE_SETTINGS) {
                int i = (int)((t_y0 - ui.list_y + ui.set_scroll) / ui.row_h);
                if (i >= 0 && i < NSET) do_action(ACT_SETTING, i);
            } else {
                int i = (int)((t_y0 - ui.list_y + ui.scroll) / ui.row_h);
                if (i >= 0 && i < ui.br.n) do_action(ACT_ROW, i);
            }
        } else do_action(h->act, h->param);
    }
}

static int pressed(const Input *in, unsigned b) {
    if (in->down & b) {
        rep_btn = b;
        rep_next = ui.t + 0.35;
        return 1;
    }
    if ((in->held & b) && rep_btn == b && ui.t >= rep_next) {
        rep_next = ui.t + 0.07;
        return 1;
    }
    return 0;
}

void app_input(const Input *in) {
    if (in->down || in->touch) ui.last_input = ui.t;
    touch(in);
    unsigned d = in->down;
    if (d & B_L) do_action(ACT_PREV, 0);
    if (d & B_R) do_action(ACT_NEXT, 0);
    if (d & B_SELECT) do_action(ACT_SCREENOFF, 0);
    if (d & B_SQUARE) do_action(ACT_ALWAYSON, 0);
    if (d & B_START) goto_page(ui.page == PAGE_SETTINGS ? PAGE_PLAYING : PAGE_SETTINGS);
    if (d & B_TRIANGLE) goto_page(ui.page == PAGE_PLAYING ? PAGE_LIBRARY : PAGE_PLAYING);

    switch (ui.page) {
    case PAGE_PLAYING:
        if (d & B_OK) do_action(ACT_PLAYPAUSE, 0);
        if (d & B_BACK) goto_page(PAGE_LIBRARY);
        if (d & B_UP) do_action(ACT_REPEAT, 0);
        if (d & B_DOWN) do_action(ACT_SHUFFLE, 0);
        if (pressed(in, B_LEFT)) player_seek(ui.ps.pos - 10), ui.pos_smooth = ui.ps.pos - 10;
        if (pressed(in, B_RIGHT)) player_seek(ui.ps.pos + 10), ui.pos_smooth = ui.ps.pos + 10;
        break;
    case PAGE_LIBRARY:
        if (pressed(in, B_UP) && ui.sel > 0) ui.sel--, ensure_visible();
        if (pressed(in, B_DOWN) && ui.sel < ui.br.n - 1) ui.sel++, ensure_visible();
        if (pressed(in, B_LEFT)) { ui.sel -= 6; if (ui.sel < 0) ui.sel = 0; ensure_visible(); }
        if (pressed(in, B_RIGHT)) { ui.sel += 6; if (ui.sel >= ui.br.n) ui.sel = ui.br.n - 1; if (ui.sel < 0) ui.sel = 0; ensure_visible(); }
        if (d & B_OK) open_row(ui.sel);
        if (d & B_BACK) go_up();
        break;
    case PAGE_SETTINGS:
        if (pressed(in, B_UP) && ui.set_sel > 0) ui.set_sel--;
        if (pressed(in, B_DOWN) && ui.set_sel < NSET - 1) ui.set_sel++;
        if (d & (B_OK | B_RIGHT)) set_change(ui.set_sel, 1);
        if (d & B_LEFT) set_change(ui.set_sel, -1);
        if (d & B_BACK) goto_page(ui.prev_page == PAGE_SETTINGS ? PAGE_PLAYING : ui.prev_page);
        break;
    }
}

/* --------- visualiser: 512-point real FFT into log-spaced bands --------- */
#define FFTN 512
static void fft(float *re, float *im) {
    for (int i = 1, j = 0; i < FFTN; i++) {
        int bit = FFTN >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= FFTN; len <<= 1) {
        float a = -6.2831853f / len, wr = cosf(a), wi = sinf(a);
        for (int i = 0; i < FFTN; i += len) {
            float cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                int p = i + k, q = p + len / 2;
                float tr = re[q] * cr - im[q] * ci, ti = re[q] * ci + im[q] * cr;
                re[q] = re[p] - tr; im[q] = im[p] - ti;
                re[p] += tr; im[p] += ti;
                float nr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = nr;
            }
        }
    }
}

static void update_viz(float dt) {
    static float s[FFTN], re[FFTN], im[FFTN], win[FFTN];
    static int inited;
    if (!inited) { for (int i = 0; i < FFTN; i++) win[i] = 0.5f - 0.5f * cosf(6.2831853f * i / (FFTN - 1)); inited = 1; }
    float l, r;
    player_viz(s, FFTN, &l, &r);
    int playing = ui_playing();
    float k = 1 - expf(-dt * 18);
    ui.lvl_l += ((playing ? l : 0) - ui.lvl_l) * k;
    ui.lvl_r += ((playing ? r : 0) - ui.lvl_r) * k;
    for (int i = 0; i < NWAVE; i++) {
        float v = playing ? s[FFTN - NWAVE * 2 + i * 2] : 0;
        ui.wave[i] += (v - ui.wave[i]) * 0.6f;
    }
    for (int i = 0; i < FFTN; i++) { re[i] = s[i] * win[i]; im[i] = 0; }
    fft(re, im);
    for (int b = 0; b < NBANDS; b++) {
        /* bins 1..255 spread logarithmically */
        int lo = (int)(1.5f * powf(170.0f, (float)b / NBANDS)), hi = (int)(1.5f * powf(170.0f, (float)(b + 1) / NBANDS));
        if (hi <= lo) hi = lo + 1;
        float m = 0;
        for (int i = lo; i < hi && i < FFTN / 2; i++) { float v = re[i] * re[i] + im[i] * im[i]; if (v > m) m = v; }
        float db = 10 * log10f(m + 1e-9f);
        float v = playing ? ui_clamp((db + 10) / 50.0f, 0, 1) : 0;
        if (v > ui.bands[b]) ui.bands[b] += (v - ui.bands[b]) * (1 - expf(-dt * 30));
        else ui.bands[b] -= dt * 1.6f;
        if (ui.bands[b] < 0) ui.bands[b] = 0;
        if (ui.bands[b] > ui.peaks[b]) ui.peaks[b] = ui.bands[b];
        else ui.peaks[b] -= dt * 0.5f;
        if (ui.peaks[b] < 0) ui.peaks[b] = 0;
    }
}

void app_update(float dt, double now) {
    ui.dt = dt;
    ui.t = now;
    player_get(&ui.ps);
    if (ui.ps.track_id != ui.track_seen) {
        ui.track_seen = ui.ps.track_id;
        ui.track_t = ui.t;
        ui.pos_smooth = ui.ps.pos;
    }
    /* positions arrive per audio grain: glide between them */
    if (ui_playing()) ui.pos_smooth += dt;
    if (fabs(ui.pos_smooth - ui.ps.pos) > 0.3 || !ui_playing()) ui.pos_smooth = ui.ps.pos;

    uint8_t *cv;
    int cs, cid;
    if (player_take_cover(&cv, &cs, &cid)) {
        if (ui.cover) gfx_tex_free(ui.cover);
        ui.cover = cv ? gfx_tex_load(cv, cs) : NULL;
        ui.cover_id = cid;
        free(cv);
    }
    if (ui.page_t < 1) ui.page_t = ui_clamp(ui.page_t + dt / 0.38f, 0, 1);

    /* list momentum + clamping */
    float *sc = ui.page == PAGE_SETTINGS ? &ui.set_scroll : &ui.scroll;
    int n = ui.page == PAGE_SETTINGS ? NSET : ui.br.n;
    if (!t_active) {
        *sc += ui.scroll_v * dt;
        ui.scroll_v *= expf(-dt * 4);
        if (fabsf(ui.scroll_v) < 5) ui.scroll_v = 0;
    }
    float maxs = n * ui.row_h - ui.list_h;
    if (maxs < 0) maxs = 0;
    if (!t_active || !t_scrolling) {
        if (*sc < 0) { *sc += (0 - *sc) * ui_clamp(dt * 12, 0, 1); ui.scroll_v = 0; }
        if (*sc > maxs) { *sc += (maxs - *sc) * ui_clamp(dt * 12, 0, 1); ui.scroll_v = 0; }
    }
    if (ui.sel >= ui.br.n) ui.sel = ui.br.n ? ui.br.n - 1 : 0;
    update_viz(dt);
}

void app_draw(void) {
    ui.nhits = 0;
    ui.hits_on = 1;
    g_skin->draw(&ui);
    ui.hits_on = 0;
}

int app_busy_anim(void) {
    return ui.page_t < 1 || t_active || ui.scroll_v != 0 || ui.t - ui.list_t < 0.8 || ui.t - ui.toast_t < 2.2 ||
           ui.t - ui.track_t < 1.2 || ui.t - ui.last_input < 1.0;
}

void app_test_set_skin(void) { apply_skin(); }
