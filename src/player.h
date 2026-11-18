#ifndef VIBE_PLAYER_H
#define VIBE_PLAYER_H
#include <stdint.h>

enum { PS_STOPPED = 0, PS_PLAYING = 1, PS_PAUSED = 2 };
enum { REPEAT_OFF = 0, REPEAT_ONE = 1, REPEAT_ALL = 2 };

typedef struct {
    int status;
    int track_id; /* bumps on every new track */
    char path[512];
    char title[256], artist[256], album[256];
    char next_title[256];
    const char *codec;
    int rate, channels, bits, bitrate;
    double pos, dur;
    int shuffle, repeat;
    int qindex, qcount;
} PlayerState;

int player_init(void);
void player_shutdown(void);
void player_play_list(char **paths, int n, int start); /* copies the list */
void player_toggle(void);
void player_stop(void);
void player_next(void);
void player_prev(void);
void player_seek(double sec);
void player_set_shuffle(int on);
void player_set_repeat(int mode);
void player_get(PlayerState *st);
/* hands over the cover bytes of the current track (caller frees); returns 1 if new */
int player_take_cover(uint8_t **data, int *size, int *track_id);
/* last n mono samples in [-1,1] and smoothed peak levels */
void player_viz(float *out, int n, float *lvl_l, float *lvl_r);

#endif
