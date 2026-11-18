#ifndef VIBE_APP_H
#define VIBE_APP_H

/* logical buttons (d-pad already rotated for portrait by the platform layer) */
enum {
    B_UP = 1 << 0, B_DOWN = 1 << 1, B_LEFT = 1 << 2, B_RIGHT = 1 << 3,
    B_OK = 1 << 4, B_BACK = 1 << 5, B_SQUARE = 1 << 6, B_TRIANGLE = 1 << 7,
    B_L = 1 << 8, B_R = 1 << 9, B_START = 1 << 10, B_SELECT = 1 << 11
};

typedef struct {
    unsigned held, down;
    int touch;      /* finger on front panel */
    float tx, ty;   /* logical coordinates */
} Input;

void app_init(const char *data_dir);
void app_shutdown(void);
void app_input(const Input *in);
void app_update(float dt, double now);
void app_draw(void);
int app_busy_anim(void);   /* transitions, scrolling: wants 60 fps */
void app_set_orientation(int o);
void app_toast(const char *msg);
void app_screen_off(void);

#endif
