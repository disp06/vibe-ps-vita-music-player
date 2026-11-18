/* Console integration: background audio, standby, display, clocks.
 * The background-playback recipe (SFO ATTRIBUTE 0x88008 + BGM port + answering
 * the shell's close event) follows ABSvita by DarthTigerson (MIT). */
#include "sys.h"
#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2/rtc.h>
#include <psp2/system_param.h>
#include <string.h>

#define IDLE_SUSPEND_S (10 * 60)

static int g_cross = 1, g_screen = 1, g_fg = 1;
static volatile int g_quit, g_awake, g_hold_oled;
static volatile unsigned g_last_active;
static int g_perf = -1;

static unsigned now_s(void) { return (unsigned)(sceKernelGetProcessTimeWide() / 1000000); }
double sys_time(void) { return sceKernelGetProcessTimeWide() / 1e6; }

/* own thread: the frame loop sleeps while the display is off */
static int tick_thread(SceSize a, void *b) {
    for (;;) {
        if (g_hold_oled) sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_OLED_DIMMING);
        else if (g_awake) sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
        else if (now_s() - g_last_active > IDLE_SUSPEND_S) {
            /* SFO disables system suspend, so standby when idle ourselves */
            g_last_active = now_s();
            scePowerRequestSuspend();
        }
        sceKernelDelayThread(1000 * 1000);
    }
    return 0;
}

int sys_init(void) {
    SceAppUtilInitParam ip;
    SceAppUtilBootParam bp;
    memset(&ip, 0, sizeof ip);
    memset(&bp, 0, sizeof bp);
    sceAppUtilInit(&ip, &bp);
    int v = 0;
    if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, &v) == 0)
        g_cross = v == SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS;
    sceAppMgrAcquireBgmPort();
    g_last_active = now_s();
    SceUID th = sceKernelCreateThread("vibe_power", tick_thread, 0x10000100, 0x2000, 0, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    sys_perf(PERF_UI);
    return 0;
}

void sys_exit(void) {
    sceAppMgrReleaseBgmPort();
    sceAppUtilShutdown();
}

typedef struct { int event; SceUID app_id; char param[56]; } AppMgrEvent;
int sceAppMgrReceiveEventNum(int *num);
int sceAppMgrReceiveEvent(AppMgrEvent *ev);

static void on_event(int e) {
    if (e == 0x20000001) g_quit = 1;               /* closed from LiveArea */
    if (e == 0x10000002) g_fg = 0;                 /* deactivated */
    if (e == 0x10000001 || e == 0x10000003) {      /* activated / resumed */
        g_fg = 1;
        g_screen = 1;
    }
}

static void poll_events(void) {
    int n = 0;
    if (sceAppMgrReceiveEventNum(&n) == 0)
        while (n-- > 0) {
            AppMgrEvent ev;
            if (sceAppMgrReceiveEvent(&ev) < 0) break;
            on_event(ev.event);
        }
    SceAppMgrAppState st;
    memset(&st, 0, sizeof st);
    if (_sceAppMgrGetAppState(&st, sizeof st, 0x03570011) < 0) return;
    while (st.systemEventNum-- > 0) {
        SceAppMgrSystemEvent ev;
        if (sceAppMgrReceiveSystemEvent(&ev) < 0) break;
        on_event(ev.systemEvent);
    }
}

void sys_tick(int playing, int input, int always_on) {
    g_awake = playing;
    g_hold_oled = always_on && g_screen;
    if (playing || input || always_on) g_last_active = now_s();
    static SceUInt64 next;
    SceUInt64 t = sceKernelGetProcessTimeWide();
    if (t < next) return;
    next = t + 100 * 1000;
    poll_events();
}

int sys_quit_requested(void) { return g_quit; }
int sys_visible(void) { return g_screen && g_fg; }
int sys_foreground(void) { return g_fg; }
int sys_screen_on(void) { return g_screen; }

void sys_screen(int on) {
    if (on == g_screen) return;
    g_screen = on;
    if (on) scePowerRequestDisplayOn();
    else scePowerRequestDisplayOff();
}

/* PERF_UI: animated UI on screen. PERF_IDLE: static UI. PERF_ECO: screen off /
 * background, only the decoder runs. */
void sys_perf(int level) {
    if (level == g_perf) return;
    g_perf = level;
    switch (level) {
    case PERF_UI:
        scePowerSetArmClockFrequency(333);
        scePowerSetBusClockFrequency(166);
        scePowerSetGpuClockFrequency(111);
        scePowerSetGpuXbarClockFrequency(111);
        break;
    case PERF_IDLE:
        scePowerSetArmClockFrequency(222);
        scePowerSetBusClockFrequency(166);
        scePowerSetGpuClockFrequency(83);
        scePowerSetGpuXbarClockFrequency(83);
        break;
    default:
        /* 166 MHz leaves ~4x headroom for FLAC/Vorbis decode on SD2Vita */
        scePowerSetArmClockFrequency(166);
        scePowerSetBusClockFrequency(111);
        scePowerSetGpuClockFrequency(41);
        scePowerSetGpuXbarClockFrequency(83);
        break;
    }
}

int sys_battery(void) { int p = scePowerGetBatteryLifePercent(); return p < 0 ? 100 : p; }
int sys_charging(void) { return scePowerIsBatteryCharging() > 0; }
int sys_enter_is_cross(void) { return g_cross; }

void sys_clock(int *h, int *m) {
    SceDateTime t;
    sceRtcGetCurrentClockLocalTime(&t);
    *h = t.hour;
    *m = t.minute;
}
