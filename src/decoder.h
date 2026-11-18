#ifndef VIBE_DECODER_H
#define VIBE_DECODER_H
#include <stdint.h>

typedef struct {
    int rate, channels;
    int64_t total;      /* frames, 0 if unknown */
    int bits;
    int bitrate;        /* kbps, 0 if unknown */
    char title[256], artist[256], album[256];
    uint8_t *cover;     /* embedded picture (jpeg/png), malloc'd */
    int cover_size;
    const char *codec;
} TrackInfo;

typedef struct Decoder {
    int (*open)(struct Decoder *d, const char *path, TrackInfo *ti);
    /* interleaved s16 output, returns frames (0 = end, <0 = error) */
    int (*read)(struct Decoder *d, int16_t *out, int frames);
    int (*seek)(struct Decoder *d, int64_t frame);
    void (*close)(struct Decoder *d);
    void *priv;
} Decoder;

/* picks a decoder by extension; returns 0 on unsupported */
int decoder_for(const char *path, Decoder *d);
int decoder_supported(const char *name);

extern const Decoder dec_mp3, dec_ogg, dec_flac, dec_wav;

void ti_set(char *dst, const char *src, int max);
void ti_vorbis_comment(TrackInfo *ti, const char *entry, int len);

#endif
