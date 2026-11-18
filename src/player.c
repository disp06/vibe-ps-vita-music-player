/* Playback engine (Vita).
 *  - decode thread: owns the decoder, fills a ~1.3 s stereo ring buffer so SD-card
 *    stalls never reach the speaker, handles load/seek/track advance.
 *  - output thread: drains the ring into a BGM audio port (keeps running in the
 *    background and with the screen off).
 * Everything shared is guarded by one lightweight mutex. */
#include "player.h"
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "decoder.h"

#define RING (1 << 16)  /* frames, stereo */
#define GRAIN 2048
#define CHUNK 1024
#define VIZ 2048

static SceKernelLwMutexWork g_mtx;
#define LOCK() sceKernelLockLwMutex(&g_mtx, 1, NULL)
#define UNLOCK() sceKernelUnlockLwMutex(&g_mtx, 1)

static int16_t g_ring[RING * 2];
static unsigned g_rd, g_wr;   /* frame counters */
static int g_ring_rate = 44100;
static int g_eof, g_gen, g_track_done;
static int64_t g_base, g_consumed;

static float g_viz[VIZ];
static unsigned g_viz_w;
static float g_lvl_l, g_lvl_r;

static char **g_q;
static int g_qn, g_qi;
static int *g_order;
static int g_shuffle, g_repeat;

static Decoder g_dec;
static int g_dec_open;
static TrackInfo g_ti;
static int g_decim = 1;
static int g_status;
static int g_track_id;
static uint8_t *g_cover;
static int g_cover_size, g_cover_new;

enum { CMD_NONE, CMD_LOAD, CMD_SEEK, CMD_STOP };
static int g_cmd, g_cmd_i;
static int64_t g_cmd_frame;
static volatile int g_quit;
static SceUID g_th_dec, g_th_out;
static int g_port = -1;

static void make_order(int keep_first) {
    free(g_order);
    g_order = malloc(sizeof(int) * (g_qn ? g_qn : 1));
    for (int i = 0; i < g_qn; i++) g_order[i] = i;
    if (g_shuffle && g_qn > 1) {
        for (int i = g_qn - 1; i > 0; i--) {
            int j = rand() % (i + 1);
            int t = g_order[i]; g_order[i] = g_order[j]; g_order[j] = t;
        }
        /* the playing track goes first so the rest of the shuffle is still ahead */
        for (int i = 0; i < g_qn; i++)
            if (g_order[i] == keep_first) { g_order[i] = g_order[0]; g_order[0] = keep_first; break; }
    }
}

static int order_pos(int qi) {
    for (int i = 0; i < g_qn; i++) if (g_order[i] == qi) return i;
    return 0;
}

static const char *base_name(const char *p) {
    const char *s = strrchr(p, '/');
    if (!s) s = strrchr(p, ':');
    return s ? s + 1 : p;
}

static void stem(char *dst, int max, const char *path) {
    ti_set(dst, base_name(path), max);
    char *dot = strrchr(dst, '.');
    if (dot) *dot = 0;
}

static void load_folder_cover(const char *path) {
    static const char *names[] = {"cover.jpg", "folder.jpg", "front.jpg", "cover.png", "folder.png", "Cover.jpg", "Folder.jpg", "AlbumArtSmall.jpg"};
    char dir[512], p[600];
    ti_set(dir, path, sizeof dir);
    char *s = strrchr(dir, '/');
    if (!s) return;
    *s = 0;
    for (unsigned i = 0; i < sizeof names / sizeof *names; i++) {
        snprintf(p, sizeof p, "%s/%s", dir, names[i]);
        FILE *f = fopen(p, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n > 0 && n < 4 * 1024 * 1024) {
            uint8_t *b = malloc(n);
            if (b && fread(b, 1, n, f) == (size_t)n) { g_ti.cover = b; g_ti.cover_size = n; fclose(f); return; }
            free(b);
        }
        fclose(f);
    }
}

/* called on the decode thread with the lock held */
static int open_track(int qi) {
    if (g_dec_open) { g_dec.close(&g_dec); g_dec_open = 0; }
    free(g_ti.cover);
    memset(&g_ti, 0, sizeof g_ti);
    if (qi < 0 || qi >= g_qn) return -1;
    if (!decoder_for(g_q[qi], &g_dec)) return -1;
    UNLOCK(); /* file I/O without blocking the UI */
    int r = g_dec.open(&g_dec, g_q[qi], &g_ti);
    if (r == 0 && !g_ti.cover) load_folder_cover(g_q[qi]);
    LOCK();
    if (r != 0) return -1;
    g_dec_open = 1;
    if (!g_ti.title[0]) stem(g_ti.title, sizeof g_ti.title, g_q[qi]);
    g_decim = 1;
    int rate = g_ti.rate;
    while (rate > 48000 && g_decim < 8) { g_decim *= 2; rate /= 2; }
    g_ring_rate = rate;
    g_qi = qi;
    g_rd = g_wr = 0;
    g_base = g_consumed = 0;
    g_eof = 0;
    g_track_done = 0;
    g_gen++;
    g_track_id++;
    free(g_cover);
    g_cover = g_ti.cover;
    g_cover_size = g_ti.cover_size;
    g_ti.cover = NULL;
    g_cover_new = 1;
    return 0;
}

/* auto: called at end of a track. returns queue index or -1 to stop */
static int next_index(int from_user) {
    if (g_qn == 0) return -1;
    if (!from_user && g_repeat == REPEAT_ONE) return g_qi;
    int p = order_pos(g_qi) + 1;
    if (p >= g_qn) {
        if (g_repeat == REPEAT_ALL || from_user) p = 0;
        else return -1;
    }
    return g_order[p];
}

static void advance(int from_user) {
    int tries = g_qn;
    int qi = next_index(from_user);
    while (qi >= 0 && tries-- > 0) {
        if (open_track(qi) == 0) { g_status = PS_PLAYING; return; }
        int p = order_pos(qi) + 1;
        if (p >= g_qn) { if (g_repeat == REPEAT_ALL || from_user) p = 0; else break; }
        qi = g_order[p];
    }
    g_status = PS_STOPPED;
    g_rd = g_wr = 0;
}

static int decode_thread(SceSize a, void *b) {
    static int16_t tmp[CHUNK * 8 * 2];
    while (!g_quit) {
        LOCK();
        if (g_cmd == CMD_LOAD) {
            g_cmd = CMD_NONE;
            int qi = g_cmd_i;
            if (open_track(qi) == 0) g_status = PS_PLAYING;
            else { g_qi = qi; advance(1); }
        } else if (g_cmd == CMD_SEEK) {
            g_cmd = CMD_NONE;
            if (g_dec_open) {
                int64_t f = g_cmd_frame;
                if (g_dec.seek(&g_dec, f * g_decim) == 0) {
                    g_rd = g_wr = 0;
                    g_base = f;
                    g_consumed = 0;
                    g_eof = 0;
                    g_gen++;
                }
            }
        } else if (g_cmd == CMD_STOP) {
            g_cmd = CMD_NONE;
            g_status = PS_STOPPED;
            if (g_dec_open) g_dec.seek(&g_dec, 0);
            g_rd = g_wr = 0;
            g_base = g_consumed = 0;
            g_eof = 0;
            g_gen++;
        }
        if (g_track_done) {
            g_track_done = 0;
            advance(0);
        }
        int busy = g_dec_open && !g_eof && g_status != PS_STOPPED && (RING - (g_wr - g_rd)) >= CHUNK;
        int gen = g_gen;
        int ch = g_ti.channels > 2 ? 2 : g_ti.channels;
        int dm = g_decim;
        UNLOCK();
        if (!busy) { sceKernelDelayThread(8000); continue; }

        int n = g_dec.read(&g_dec, tmp, CHUNK * dm);
        LOCK();
        if (gen != g_gen) { UNLOCK(); continue; } /* seek/load raced us: drop it */
        if (n <= 0) { g_eof = 1; UNLOCK(); continue; }
        int frames = n / dm;
        for (int i = 0; i < frames; i++) {
            int l = 0, r = 0;
            for (int k = 0; k < dm; k++) {
                const int16_t *s = tmp + (size_t)(i * dm + k) * ch;
                l += s[0];
                r += ch > 1 ? s[1] : s[0];
            }
            unsigned w = (g_wr + i) & (RING - 1);
            g_ring[w * 2] = l / dm;
            g_ring[w * 2 + 1] = r / dm;
        }
        g_wr += frames;
        UNLOCK();
    }
    return 0;
}

static int output_thread(SceSize a, void *b) {
    static int16_t out[GRAIN * 2];
    int cur_rate = 0;
    while (!g_quit) {
        LOCK();
        if (g_status != PS_PLAYING) { UNLOCK(); sceKernelDelayThread(10000); continue; }
        unsigned avail = g_wr - g_rd;
        int take = 0;
        if (avail >= GRAIN) take = GRAIN;
        else if (g_eof && avail > 0) take = avail;
        else if (g_eof && avail == 0 && !g_track_done) { g_track_done = 1; UNLOCK(); sceKernelDelayThread(2000); continue; }
        if (!take) { UNLOCK(); sceKernelDelayThread(3000); continue; }
        for (int i = 0; i < take; i++) {
            unsigned r = (g_rd + i) & (RING - 1);
            out[i * 2] = g_ring[r * 2];
            out[i * 2 + 1] = g_ring[r * 2 + 1];
        }
        memset(out + take * 2, 0, (GRAIN - take) * 4);
        g_rd += take;
        int gen = g_gen, rate = g_ring_rate;
        UNLOCK();

        if (rate != cur_rate) {
            if (g_port < 0) g_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, GRAIN, rate, SCE_AUDIO_OUT_MODE_STEREO);
            else sceAudioOutSetConfig(g_port, GRAIN, rate, SCE_AUDIO_OUT_MODE_STEREO);
            if (g_port >= 0) {
                int vol[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB};
                sceAudioOutSetVolume(g_port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);
            }
            cur_rate = rate;
        }
        /* visualiser tap (cheap: one pass, no FFT here) */
        float pl = 0, pr = 0;
        for (int i = 0; i < take; i += 2) {
            float l = out[i * 2] / 32768.0f, r = out[i * 2 + 1] / 32768.0f;
            g_viz[g_viz_w++ & (VIZ - 1)] = (l + r) * 0.5f;
            if (l < 0) l = -l;
            if (r < 0) r = -r;
            if (l > pl) pl = l;
            if (r > pr) pr = r;
        }
        g_lvl_l = g_lvl_l * 0.6f + pl * 0.4f;
        g_lvl_r = g_lvl_r * 0.6f + pr * 0.4f;
        if (g_port >= 0) sceAudioOutOutput(g_port, out);
        else sceKernelDelayThread(GRAIN * 1000000LL / rate);
        LOCK();
        if (gen == g_gen) g_consumed += take;
        UNLOCK();
    }
    return 0;
}

int player_init(void) {
    sceKernelCreateLwMutex(&g_mtx, "vibe_player", 0, 0, NULL);
    g_th_dec = sceKernelCreateThread("vibe_decode", decode_thread, 0x10000100 - 8, 0x10000, 0, 0, NULL);
    g_th_out = sceKernelCreateThread("vibe_output", output_thread, 0x10000100 - 16, 0x4000, 0, 0, NULL);
    if (g_th_dec < 0 || g_th_out < 0) return -1;
    sceKernelStartThread(g_th_dec, 0, NULL);
    sceKernelStartThread(g_th_out, 0, NULL);
    return 0;
}

void player_shutdown(void) {
    g_quit = 1;
    sceKernelWaitThreadEnd(g_th_dec, NULL, NULL);
    sceKernelWaitThreadEnd(g_th_out, NULL, NULL);
    if (g_dec_open) g_dec.close(&g_dec);
    if (g_port >= 0) sceAudioOutReleasePort(g_port);
}

void player_play_list(char **paths, int n, int start) {
    LOCK();
    for (int i = 0; i < g_qn; i++) free(g_q[i]);
    free(g_q);
    g_q = malloc(sizeof(char *) * (n ? n : 1));
    for (int i = 0; i < n; i++) g_q[i] = strdup(paths[i]);
    g_qn = n;
    make_order(start);
    g_cmd = CMD_LOAD;
    g_cmd_i = start;
    UNLOCK();
}

void player_toggle(void) {
    LOCK();
    if (g_status == PS_PLAYING) g_status = PS_PAUSED;
    else if (g_status == PS_PAUSED) g_status = PS_PLAYING;
    else if (g_qn) { g_cmd = CMD_LOAD; g_cmd_i = g_qi; }
    UNLOCK();
}

void player_stop(void) { LOCK(); g_cmd = CMD_STOP; UNLOCK(); }

void player_next(void) {
    LOCK();
    int cur = g_qi;
    if (g_cmd == CMD_LOAD) g_qi = g_cmd_i; /* stack quick presses */
    int qi = next_index(1);
    g_qi = cur;
    if (qi >= 0) { g_cmd = CMD_LOAD; g_cmd_i = qi; }
    UNLOCK();
}

void player_prev(void) {
    LOCK();
    double pos = g_ring_rate ? (double)(g_base + g_consumed) / g_ring_rate : 0;
    if (pos > 3.0 || g_qn == 0) { g_cmd = CMD_SEEK; g_cmd_frame = 0; }
    else {
        int p = order_pos(g_cmd == CMD_LOAD ? g_cmd_i : g_qi) - 1;
        if (p < 0) p = g_qn - 1;
        g_cmd = CMD_LOAD;
        g_cmd_i = g_order[p];
    }
    UNLOCK();
}

void player_seek(double sec) {
    LOCK();
    if (sec < 0) sec = 0;
    g_cmd = CMD_SEEK;
    g_cmd_frame = (int64_t)(sec * g_ring_rate);
    UNLOCK();
}

void player_set_shuffle(int on) {
    LOCK();
    g_shuffle = on;
    make_order(g_qi);
    UNLOCK();
}

void player_set_repeat(int m) { LOCK(); g_repeat = m; UNLOCK(); }

void player_get(PlayerState *st) {
    LOCK();
    memset(st, 0, sizeof *st);
    st->status = g_status;
    st->track_id = g_track_id;
    st->shuffle = g_shuffle;
    st->repeat = g_repeat;
    st->qindex = g_qn ? order_pos(g_qi) : 0;
    st->qcount = g_qn;
    if (g_qn && g_qi < g_qn) {
        ti_set(st->path, g_q[g_qi], sizeof st->path);
        int n = next_index(0);
        if (g_repeat == REPEAT_ONE) n = next_index(1);
        if (n >= 0) stem(st->next_title, sizeof st->next_title, g_q[n]);
    }
    if (g_dec_open) {
        memcpy(st->title, g_ti.title, sizeof st->title);
        memcpy(st->artist, g_ti.artist, sizeof st->artist);
        memcpy(st->album, g_ti.album, sizeof st->album);
        st->codec = g_ti.codec;
        st->rate = g_ti.rate;
        st->channels = g_ti.channels;
        st->bits = g_ti.bits;
        st->bitrate = g_ti.bitrate;
        st->dur = g_ti.rate ? (double)g_ti.total / g_ti.rate : 0;
        st->pos = g_ring_rate ? (double)(g_base + g_consumed) / g_ring_rate : 0;
        if (g_cmd == CMD_SEEK) st->pos = (double)g_cmd_frame / g_ring_rate;
        if (st->dur > 0 && st->pos > st->dur) st->pos = st->dur;
    }
    UNLOCK();
}

int player_take_cover(uint8_t **data, int *size, int *track_id) {
    LOCK();
    int r = g_cover_new;
    if (r) {
        *data = g_cover;
        *size = g_cover_size;
        *track_id = g_track_id;
        g_cover = NULL;
        g_cover_size = 0;
        g_cover_new = 0;
    }
    UNLOCK();
    return r;
}

void player_viz(float *out, int n, float *l, float *r) {
    unsigned w = g_viz_w;
    if (n > VIZ) n = VIZ;
    for (int i = 0; i < n; i++) out[i] = g_viz[(w - n + i) & (VIZ - 1)];
    *l = g_lvl_l;
    *r = g_lvl_r;
}
