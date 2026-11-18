/* RIFF WAVE: PCM 8/16/24/32-bit and 32-bit float, plus LIST/INFO tags */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "decoder.h"

typedef struct { FILE *fp; long data_off; int64_t frames, pos; int ch, outch, bytes, isfloat; uint8_t *tmp; } W;

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return p[0] | p[1] << 8; }

static int w_open(Decoder *d, const char *path, TrackInfo *ti) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    uint8_t h[12];
    if (fread(h, 1, 12, fp) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { fclose(fp); return -1; }
    W *w = calloc(1, sizeof *w);
    w->fp = fp;
    int fmt_ok = 0, bits = 0;
    uint8_t ck[8];
    while (fread(ck, 1, 8, fp) == 8) {
        uint32_t sz = le32(ck + 4);
        long next = ftell(fp) + sz + (sz & 1);
        if (!memcmp(ck, "fmt ", 4) && sz >= 16) {
            uint8_t f[40] = {0};
            fread(f, 1, sz < 40 ? sz : 40, fp);
            int tag = le16(f);
            if (tag == 0xFFFE && sz >= 26) tag = le16(f + 24);
            w->ch = le16(f + 2);
            ti->rate = le32(f + 4);
            bits = le16(f + 14);
            w->isfloat = tag == 3;
            fmt_ok = (tag == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (tag == 3 && bits == 32);
        } else if (!memcmp(ck, "LIST", 4) && sz > 4 && sz < 65536) {
            uint8_t *l = malloc(sz);
            if (l && fread(l, 1, sz, fp) == sz && !memcmp(l, "INFO", 4)) {
                for (uint32_t o = 4; o + 8 <= sz;) {
                    uint32_t n = le32(l + o + 4);
                    if (o + 8 + n > sz) break;
                    char v[256];
                    int k = n < 255 ? n : 255;
                    memcpy(v, l + o + 8, k);
                    v[k] = 0;
                    if (!memcmp(l + o, "INAM", 4)) ti_set(ti->title, v, sizeof ti->title);
                    if (!memcmp(l + o, "IART", 4)) ti_set(ti->artist, v, sizeof ti->artist);
                    if (!memcmp(l + o, "IPRD", 4)) ti_set(ti->album, v, sizeof ti->album);
                    o += 8 + n + (n & 1);
                }
            }
            free(l);
        } else if (!memcmp(ck, "data", 4) && fmt_ok) {
            w->data_off = ftell(fp);
            w->bytes = bits / 8;
            w->frames = sz / (w->bytes * w->ch);
            /* keep scanning for a trailing LIST chunk only if the file is seekable */
        }
        if (fseek(fp, next, SEEK_SET)) break;
    }
    if (!fmt_ok || !w->data_off || w->ch < 1) { fclose(fp); free(w); return -1; }
    fseek(fp, w->data_off, SEEK_SET);
    w->outch = w->ch > 2 ? 2 : w->ch;
    ti->channels = w->ch;
    ti->bits = bits;
    ti->total = w->frames;
    ti->bitrate = ti->rate * w->ch * bits / 1000;
    ti->codec = "WAV";
    w->tmp = malloc(4096 * w->ch * w->bytes);
    d->priv = w;
    return 0;
}

static int w_read(Decoder *d, int16_t *out, int frames) {
    W *w = d->priv;
    int done = 0;
    while (done < frames && w->pos < w->frames) {
        int k = frames - done;
        if (k > 4096) k = 4096;
        if (k > w->frames - w->pos) k = (int)(w->frames - w->pos);
        int got = fread(w->tmp, w->ch * w->bytes, k, w->fp);
        if (got <= 0) break;
        for (int i = 0; i < got; i++)
            for (int c = 0; c < w->outch; c++) {
                const uint8_t *p = w->tmp + (i * w->ch + c) * w->bytes;
                int v;
                switch (w->bytes) {
                case 1: v = (p[0] - 128) << 8; break;
                case 2: v = (int16_t)le16(p); break;
                case 3: v = (int16_t)(p[1] | p[2] << 8); break;
                default:
                    if (w->isfloat) {
                        float f;
                        memcpy(&f, p, 4);
                        f *= 32767.0f;
                        v = f > 32767 ? 32767 : f < -32768 ? -32768 : (int)f;
                    } else v = (int16_t)le16(p + 2);
                }
                out[(done + i) * w->outch + c] = (int16_t)v;
            }
        done += got;
        w->pos += got;
    }
    return done;
}

static int w_seek(Decoder *d, int64_t f) {
    W *w = d->priv;
    if (f > w->frames) f = w->frames;
    w->pos = f;
    return fseek(w->fp, w->data_off + (long)(f * w->ch * w->bytes), SEEK_SET);
}

static void w_close(Decoder *d) {
    W *w = d->priv;
    if (!w) return;
    fclose(w->fp);
    free(w->tmp);
    free(w);
    d->priv = NULL;
}

const Decoder dec_wav = {w_open, w_read, w_seek, w_close, NULL};
