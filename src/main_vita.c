/* Vibe - PS Vita entry point: input mapping, frame pacing, screen/power policy */
#include <math.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/touch.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "app.h"
#include "font.h"
#include "gfx.h"
#include "player.h"
#include "sys.h"
#include "ui.h"

#define DATA_DIR "ux0:data/Vibe"

int _newlib_heap_size_user = 64 * 1024 * 1024;

static unsigned map_buttons(unsigned b, int orient, int cross_enter) {
    unsigned o = 0;
    unsigned up = SCE_CTRL_UP, down = SCE_CTRL_DOWN, left = SCE_CTRL_LEFT, right = SCE_CTRL_RIGHT;
    if (orient == ORIENT_PORTRAIT) { up = SCE_CTRL_RIGHT; down = SCE_CTRL_LEFT; left = SCE_CTRL_UP; right = SCE_CTRL_DOWN; }
    else if (orient == ORIENT_PORTRAIT_FLIP) { up = SCE_CTRL_LEFT; down = SCE_CTRL_RIGHT; left = SCE_CTRL_DOWN; right = SCE_CTRL_UP; }
    if (b & up) o |= B_UP;
    if (b & down) o |= B_DOWN;
    if (b & left) o |= B_LEFT;
    if (b & right) o |= B_RIGHT;
    unsigned ok = cross_enter ? SCE_CTRL_CROSS : SCE_CTRL_CIRCLE, back = cross_enter ? SCE_CTRL_CIRCLE : SCE_CTRL_CROSS;
    if (b & ok) o |= B_OK;
    if (b & back) o |= B_BACK;
    if (b & SCE_CTRL_SQUARE) o |= B_SQUARE;
    if (b & SCE_CTRL_TRIANGLE) o |= B_TRIANGLE;
    if (b & SCE_CTRL_LTRIGGER) o |= B_L;
    if (b & SCE_CTRL_RTRIGGER) o |= B_R;
    if (b & SCE_CTRL_START) o |= B_START;
    if (b & SCE_CTRL_SELECT) o |= B_SELECT;
    return o;
}

static void map_touch(float sx, float sy, int orient, float *lx, float *ly) {
    switch (orient) {
    case ORIENT_PORTRAIT: *lx = sy; *ly = 960 - sx; break;
    case ORIENT_PORTRAIT_FLIP: *lx = 544 - sy; *ly = sx; break;
    default: *lx = sx; *ly = sy; break;
    }
}

int main(void) {
    srand((unsigned)time(NULL));
    sceIoMkdir("ux0:data", 0777);
    sceIoMkdir(DATA_DIR, 0777);
    sys_init();
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);

    gfx_init();
    font_init("app0:assets/fonts");
    player_init();
    app_init(DATA_DIR);

    unsigned prev_raw = 0;
    int prev_touch = 0;
    int swallow = 0; /* input that woke the screen is not passed on */
    double last = sys_time();
    double last_input = last;
    int was_playing = 0;

    while (!sys_quit_requested()) {
        double now = sys_time();
        float dt = (float)(now - last);
        if (dt > 0.25f) dt = 0.25f;
        last = now;

        SceCtrlData pad;
        memset(&pad, 0, sizeof pad);
        sceCtrlPeekBufferPositive(0, &pad, 1);
        SceTouchData td;
        memset(&td, 0, sizeof td);
        sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1);
        unsigned raw = pad.buttons;
        unsigned raw_down = raw & ~prev_raw;
        int touching = td.reportNum > 0;
        int any = raw_down || (touching && !prev_touch);
        prev_raw = raw;
        prev_touch = touching;

        int playing = ui.ps.status == PS_PLAYING;

        /* display policy */
        if (!sys_screen_on()) {
            if (any) { sys_screen(1); sys_perf(PERF_UI); swallow = 1; last_input = now; }
        } else if (cfg.screen_off && !cfg.always_on && playing && sys_foreground() &&
                   now - last_input > cfg.screen_off && now - ui.toast_t > 2) {
            sys_screen(0);
        }
        if (any) last_input = now;
        if (swallow && !raw && !touching) swallow = 0;
        sys_tick(playing, any, cfg.always_on);

        Input in;
        memset(&in, 0, sizeof in);
        if (!swallow && sys_visible()) {
            in.held = map_buttons(raw, cfg.orient, sys_enter_is_cross());
            in.down = map_buttons(raw_down, cfg.orient, sys_enter_is_cross());
            if (touching) {
                in.touch = 1;
                map_touch(td.report[0].x / 2.0f, td.report[0].y / 2.0f, cfg.orient, &in.tx, &in.ty);
            }
        } else if (!sys_visible()) {
            /* background / screen off: L/R still skip tracks */
            unsigned d = map_buttons(raw_down, cfg.orient, 1);
            if (sys_foreground() && !swallow) { if (d & B_L) player_prev(); if (d & B_R) player_next(); }
        }

        if (!sys_visible()) {
            /* nothing to see: keep the decoder fed at low clocks and don't render */
            sys_perf(PERF_ECO);
            app_update(dt, now);
            sceKernelDelayThread(50 * 1000);
            was_playing = playing;
            continue;
        }
        if (playing && !was_playing) last_input = now; /* start the screen-off countdown */
        was_playing = playing;

        app_input(&in);
        app_update(dt, now);
        int busy = app_busy_anim();
        sys_perf(busy ? PERF_UI : PERF_IDLE);

        if (cfg.always_on && cfg.pixel_shift)
            gfx_set_shift(3.0f * sinf((float)now / 97.0f), 3.0f * sinf((float)now / 131.0f));
        else
            gfx_set_shift(0, 0);

        font_frame_begin();
        gfx_begin(RGB(0, 0, 0));
        app_draw();
        gfx_end();
        if (!cfg.fps60 && !busy) sceDisplayWaitVblankStart(); /* 30 fps when calm */
    }

    app_shutdown();
    player_shutdown();
    gfx_fini();
    sys_exit();
    sceKernelExitProcess(0);
    return 0;
}
