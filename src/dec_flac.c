#include <FLAC/stream_decoder.h>
#include <stdlib.h>
#include <string.h>
#include "decoder.h"

typedef struct {
    FLAC__StreamDecoder *dec;
    TrackInfo *ti;
    int16_t *buf; /* decoded but not yet handed out, stereo-or-mono s16 */
    int cap, len, pos, ch, bits;
    int err;
} F;

static FLAC__StreamDecoderWriteStatus wr(const FLAC__StreamDecoder *dec, const FLAC__Frame *fr,
                                         const FLAC__int32 *const b[], void *ud) {
    F *f = ud;
    int n = fr->header.blocksize, ch = f->ch;
    int sh = fr->header.bits_per_sample - 16;
    if (f->len + n > f->cap) {
        int nc = (f->len + n) * 2;
        int16_t *nb = realloc(f->buf, (size_t)nc * ch * 2);
        if (!nb) return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
        f->buf = nb;
        f->cap = nc;
    }
    int16_t *o = f->buf + (size_t)f->len * ch;
    for (int i = 0; i < n; i++)
        for (int c = 0; c < ch; c++) {
            int v = b[c][i];
            v = sh > 0 ? v >> sh : v << -sh;
            o[i * ch + c] = (int16_t)v;
        }
    f->len += n;
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void md(const FLAC__StreamDecoder *dec, const FLAC__StreamMetadata *m, void *ud) {
    F *f = ud;
    TrackInfo *ti = f->ti;
    if (!ti) return;
    if (m->type == FLAC__METADATA_TYPE_STREAMINFO) {
        ti->rate = m->data.stream_info.sample_rate;
        ti->channels = m->data.stream_info.channels;
        ti->bits = m->data.stream_info.bits_per_sample;
        ti->total = m->data.stream_info.total_samples;
    } else if (m->type == FLAC__METADATA_TYPE_VORBIS_COMMENT) {
        for (unsigned i = 0; i < m->data.vorbis_comment.num_comments; i++)
            ti_vorbis_comment(ti, (const char *)m->data.vorbis_comment.comments[i].entry,
                              m->data.vorbis_comment.comments[i].length);
    } else if (m->type == FLAC__METADATA_TYPE_PICTURE && !ti->cover) {
        const FLAC__StreamMetadata_Picture *p = &m->data.picture;
        if (p->data_length && (p->type == 3 || !ti->cover)) {
            ti->cover = malloc(p->data_length);
            if (ti->cover) { memcpy(ti->cover, p->data, p->data_length); ti->cover_size = p->data_length; }
        }
    }
}

static void er(const FLAC__StreamDecoder *dec, FLAC__StreamDecoderErrorStatus st, void *ud) { ((F *)ud)->err++; }

static int f_open(Decoder *d, const char *path, TrackInfo *ti) {
    F *f = calloc(1, sizeof *f);
    if (!f) return -1;
    f->dec = FLAC__stream_decoder_new();
    f->ti = ti;
    f->ch = 2;
    FLAC__stream_decoder_set_metadata_respond(f->dec, FLAC__METADATA_TYPE_VORBIS_COMMENT);
    FLAC__stream_decoder_set_metadata_respond(f->dec, FLAC__METADATA_TYPE_PICTURE);
    if (FLAC__stream_decoder_init_file(f->dec, path, wr, md, er, f) != FLAC__STREAM_DECODER_INIT_STATUS_OK ||
        !FLAC__stream_decoder_process_until_end_of_metadata(f->dec) || ti->rate == 0) {
        FLAC__stream_decoder_delete(f->dec);
        free(f);
        return -1;
    }
    f->ch = ti->channels > 2 ? 2 : ti->channels;
    f->bits = ti->bits;
    f->ti = NULL;
    ti->codec = "FLAC";
    d->priv = f;
    return 0;
}

static int f_read(Decoder *d, int16_t *out, int frames) {
    F *f = d->priv;
    int done = 0;
    while (done < frames) {
        if (f->pos >= f->len) {
            f->pos = f->len = 0;
            if (FLAC__stream_decoder_get_state(f->dec) == FLAC__STREAM_DECODER_END_OF_STREAM) break;
            if (!FLAC__stream_decoder_process_single(f->dec)) break;
            if (f->len == 0 && FLAC__stream_decoder_get_state(f->dec) == FLAC__STREAM_DECODER_END_OF_STREAM) break;
            continue;
        }
        int k = f->len - f->pos;
        if (k > frames - done) k = frames - done;
        memcpy(out + (size_t)done * f->ch, f->buf + (size_t)f->pos * f->ch, (size_t)k * f->ch * 2);
        f->pos += k;
        done += k;
    }
    return done;
}

static int f_seek(Decoder *d, int64_t fr) {
    F *f = d->priv;
    f->pos = f->len = 0;
    if (!FLAC__stream_decoder_seek_absolute(f->dec, fr)) {
        if (FLAC__stream_decoder_get_state(f->dec) == FLAC__STREAM_DECODER_SEEK_ERROR) FLAC__stream_decoder_flush(f->dec);
        return -1;
    }
    return 0;
}

static void f_close(Decoder *d) {
    F *f = d->priv;
    if (!f) return;
    FLAC__stream_decoder_finish(f->dec);
    FLAC__stream_decoder_delete(f->dec);
    free(f->buf);
    free(f);
    d->priv = NULL;
}

const Decoder dec_flac = {f_open, f_read, f_seek, f_close, NULL};
