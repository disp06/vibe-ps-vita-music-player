#ifndef VIBE_SYS_H
#define VIBE_SYS_H

enum { PERF_UI = 0, PERF_IDLE = 1, PERF_ECO = 2 };

int sys_init(void);
void sys_exit(void);
/* call every loop iteration; playing keeps the console out of standby,
 * always_on additionally holds the OLED awake */
void sys_tick(int playing, int input, int always_on);
int sys_quit_requested(void);
int sys_visible(void);        /* screen on and app in the foreground */
int sys_foreground(void);
void sys_screen(int on);
int sys_screen_on(void);
void sys_perf(int level);
int sys_battery(void);
int sys_charging(void);
void sys_clock(int *h, int *m);
int sys_enter_is_cross(void);
double sys_time(void);        /* seconds, monotonic */

#endif
