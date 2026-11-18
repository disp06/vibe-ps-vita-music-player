#include <stdio.h>
#include <mpg123.h>
#include <stdlib.h>
#include <string.h>
#include "decoder.h"

static int g_inited;

static void v2text(char *dst, int max, mpg123_string *s) {
    if (s && s->fill > 1 && !dst[0]) ti_set(dst, s->p, max);
}

static int m_open(Decoder *d, const char *path, TrackInfo *ti) {
    if (!g_inited) { mpg123_init(); g_inited = 1; }
    int err;
    mpg123_handle *mh = mpg123_new(NULL, &err);
    if (!mh) return -1;
    mpg123_param(mh, MPG123_ADD_FLAGS, MPG123_PICTURE | MPG123_GAPLESS | MPG123_SEEKBUFFER, 0);
    mpg123_param(mh, MPG123_INDEX_SIZE, -1000, 0);
    mpg123_format_none(mh);
    const long *rates;
    size_t nr;
    mpg123_rates(&rates, &nr);
    for (size_t i = 0; i < nr; i++) mpg123_format(mh, rates[i], MPG123_MONO | MPG123_STEREO, MPG123_ENC_SIGNED_16);
    if (mpg123_open(mh, path) != MPG123_OK) { mpg123_delete(mh); return -1; }
    long rate;
    int ch, enc;
    if (mpg123_getformat(mh, &rate, &ch, &enc) != MPG123_OK) { mpg123_close(mh); mpg123_delete(mh); return -1; }
    mpg123_format_none(mh);
    mpg123_format(mh, rate, ch, MPG123_ENC_SIGNED_16);
    ti->rate = rate;
    ti->channels = ch;
    ti->bits = 16;
    off_t len = mpg123_length(mh);
    ti->total = len > 0 ? len : 0;
    struct mpg123_frameinfo fi;
    if (mpg123_info(mh, &fi) == MPG123_OK) ti->bitrate = fi.bitrate;
    ti->codec = "MP3";

    mpg123_id3v1 *v1 = NULL;
    mpg123_id3v2 *v2 = NULL;
    if (mpg123_meta_check(mh) & MPG123_ID3 && mpg123_id3(mh, &v1, &v2) == MPG123_OK) {
        if (v2) {
            v2text(ti->title, sizeof ti->title, v2->title);
            v2text(ti->artist, sizeof ti->artist, v2->artist);
            v2text(ti->album, sizeof ti->album, v2->album);
            mpg123_picture *best = NULL;
            for (size_t i = 0; i < v2->pictures; i++) {
                mpg123_picture *p = &v2->picture[i];
                if (!p->data || !p->size) continue;
                if (!best || p->type == 3) best = p;
            }
            if (best) {
                ti->cover = malloc(best->size);
                if (ti->cover) { memcpy(ti->cover, best->data, best->size); ti->cover_size = best->size; }
            }
        }
        if (v1) {
            char tmp[32];
            if (!ti->title[0]) { memcpy(tmp, v1->title, 30); tmp[30] = 0; ti_set(ti->title, tmp, sizeof ti->title); }
            if (!ti->artist[0]) { memcpy(tmp, v1->artist, 30); tmp[30] = 0; ti_set(ti->artist, tmp, sizeof ti->artist); }
            if (!ti->album[0]) { memcpy(tmp, v1->album, 30); tmp[30] = 0; ti_set(ti->album, tmp, sizeof ti->album); }
        }
    }
    d->priv = mh;
    return 0;
}

static int m_read(Decoder *d, int16_t *out, int frames) {
    mpg123_handle *mh = d->priv;
    long rate;
    int ch, enc;
    mpg123_getformat(mh, &rate, &ch, &enc);
    size_t want = (size_t)frames * ch * 2, got = 0;
    for (int tries = 0; tries < 8; tries++) {
        size_t done = 0;
        int r = mpg123_read(mh, (unsigned char *)out + got, want - got, &done);
        got += done;
        if (got >= want) break;
        if (r == MPG123_DONE) break;
        if (r == MPG123_NEW_FORMAT) continue;
        if (r != MPG123_OK && r != MPG123_NEED_MORE) { if (!got) return -1; break; }
    }
    return (int)(got / (ch * 2));
}

static int m_seek(Decoder *d, int64_t frame) { return mpg123_seek(d->priv, (off_t)frame, SEEK_SET) < 0 ? -1 : 0; }

static void m_close(Decoder *d) {
    if (!d->priv) return;
    mpg123_close(d->priv);
    mpg123_delete(d->priv);
    d->priv = NULL;
}

const Decoder dec_mp3 = {m_open, m_read, m_seek, m_close, NULL};
