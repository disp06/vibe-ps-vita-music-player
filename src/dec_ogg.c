#include <stdlib.h>
#include <string.h>
#include <vorbis/vorbisfile.h>
#include "decoder.h"

static int o_open(Decoder *d, const char *path, TrackInfo *ti) {
    OggVorbis_File *vf = calloc(1, sizeof *vf);
    if (!vf) return -1;
    if (ov_fopen(path, vf) != 0) { free(vf); return -1; }
    vorbis_info *vi = ov_info(vf, -1);
    ti->rate = vi->rate;
    ti->channels = vi->channels;
    ti->bits = 16;
    ti->bitrate = vi->bitrate_nominal > 0 ? vi->bitrate_nominal / 1000 : 0;
    ogg_int64_t t = ov_pcm_total(vf, -1);
    ti->total = t > 0 ? t : 0;
    ti->codec = "OGG";
    vorbis_comment *vc = ov_comment(vf, -1);
    for (int i = 0; vc && i < vc->comments; i++) ti_vorbis_comment(ti, vc->user_comments[i], vc->comment_lengths[i]);
    d->priv = vf;
    return 0;
}

static int o_read(Decoder *d, int16_t *out, int frames) {
    OggVorbis_File *vf = d->priv;
    int ch = ov_info(vf, -1)->channels;
    int want = frames * ch * 2, got = 0, bs;
    /* >2 channels: decode into scratch and keep the front pair */
    if (ch > 2) {
        static int16_t tmp[4096 * 8];
        int n = 0;
        while (n < frames) {
            int k = frames - n;
            if (k * ch > 4096 * 8) k = 4096 * 8 / ch;
            long r = ov_read(vf, (char *)tmp, k * ch * 2, 0, 2, 1, &bs);
            if (r <= 0) break;
            int fr = r / (ch * 2);
            for (int i = 0; i < fr; i++) { out[(n + i) * 2] = tmp[i * ch]; out[(n + i) * 2 + 1] = tmp[i * ch + 1]; }
            n += fr;
        }
        return n;
    }
    while (got < want) {
        long r = ov_read(vf, (char *)out + got, want - got, 0, 2, 1, &bs);
        if (r == OV_HOLE) continue;
        if (r <= 0) break;
        got += r;
    }
    return got / (ch * 2);
}

static int o_seek(Decoder *d, int64_t f) { return ov_pcm_seek(d->priv, f) ? -1 : 0; }

static void o_close(Decoder *d) {
    if (!d->priv) return;
    ov_clear(d->priv);
    free(d->priv);
    d->priv = NULL;
}

const Decoder dec_ogg = {o_open, o_read, o_seek, o_close, NULL};
