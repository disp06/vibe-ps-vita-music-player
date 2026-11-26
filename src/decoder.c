#include "decoder.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static const char *ext(const char *p) {
    const char *e = strrchr(p, '.');
    return e ? e + 1 : "";
}

static int ieq(const char *a, const char *b) {
    while (*a && *b) if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return 0;
    return *a == *b;
}

int decoder_supported(const char *name) {
    const char *e = ext(name);
    return ieq(e, "mp3") || ieq(e, "ogg") || ieq(e, "oga") || ieq(e, "flac") || ieq(e, "wav");
}

int decoder_for(const char *path, Decoder *d) {
    const char *e = ext(path);
    if (ieq(e, "mp3")) *d = dec_mp3;
    else if (ieq(e, "ogg") || ieq(e, "oga")) *d = dec_ogg;
    else if (ieq(e, "flac")) *d = dec_flac;
    else if (ieq(e, "wav")) *d = dec_wav;
    else return 0;
    d->priv = NULL;
    return 1;
}

static int utf8_ok(const unsigned char *s, int n) {
    for (int i = 0; i < n;) {
        unsigned c = s[i];
        if (c < 0x80) { i++; continue; }
        int k = c >= 0xC2 && c <= 0xDF ? 1 : c >= 0xE0 && c <= 0xEF ? 2 : c >= 0xF0 && c <= 0xF4 ? 3 : -1;
        if (k < 0 || i + k >= n) return 0;
        for (int j = 1; j <= k; j++)
            if ((s[i + j] & 0xC0) != 0x80) return 0;
        i += k + 1;
    }
    return 1;
}

/* tag text becomes UTF-8: bytes that are not valid UTF-8 are taken as Latin-1
 * (ID3v1, RIFF INFO), and the result is cut on a character boundary */
void ti_set(char *dst, const char *src, int max) {
    char raw[1024], conv[2048];
    int n = 0;
    while (n < 1000 && src[n]) { raw[n] = src[n]; n++; }
    const char *s = raw;
    int m = n;
    if (!utf8_ok((const unsigned char *)raw, n)) {
        int o = 0;
        for (int i = 0; i < n; i++) {
            unsigned char c = raw[i];
            if (c < 0x80) conv[o++] = c;
            else { conv[o++] = 0xC0 | (c >> 6); conv[o++] = 0x80 | (c & 0x3F); }
        }
        s = conv;
        m = o;
    }
    while (m > 0 && (s[m - 1] == ' ' || s[m - 1] == '\r' || s[m - 1] == '\n')) m--;
    if (m > max - 1) {
        m = max - 1;
        while (m > 0 && ((unsigned char)s[m] & 0xC0) == 0x80) m--;
    }
    memcpy(dst, s, m);
    dst[m] = 0;
}

static int b64v(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

/* FLAC-style PICTURE block -> image bytes */
static void take_picture_block(TrackInfo *ti, const uint8_t *b, int n) {
    if (ti->cover || n < 32) return;
    int o = 4;
    uint32_t ml = be32(b + o); o += 4 + ml;
    if (o + 4 > n) return;
    uint32_t dl = be32(b + o); o += 4 + dl;
    o += 16;
    if (o + 4 > n) return;
    uint32_t len = be32(b + o); o += 4;
    if (o + (int)len > n || len == 0) return;
    ti->cover = malloc(len);
    if (!ti->cover) return;
    memcpy(ti->cover, b + o, len);
    ti->cover_size = len;
}

void ti_vorbis_comment(TrackInfo *ti, const char *e, int len) {
    char key[32];
    int i = 0;
    while (i < len && i < 31 && e[i] != '=') { key[i] = toupper((unsigned char)e[i]); i++; }
    key[i] = 0;
    if (i >= len || e[i] != '=') return;
    const char *v = e + i + 1;
    int vl = len - i - 1;
    char buf[256];
    int n = vl < 255 ? vl : 255;
    memcpy(buf, v, n);
    buf[n] = 0;
    if (!strcmp(key, "TITLE") && !ti->title[0]) ti_set(ti->title, buf, sizeof ti->title);
    else if (!strcmp(key, "ARTIST") && !ti->artist[0]) ti_set(ti->artist, buf, sizeof ti->artist);
    else if (!strcmp(key, "ALBUM") && !ti->album[0]) ti_set(ti->album, buf, sizeof ti->album);
    else if (!strcmp(key, "METADATA_BLOCK_PICTURE") && !ti->cover && vl > 64) {
        uint8_t *raw = malloc(vl * 3 / 4 + 4);
        if (!raw) return;
        int acc = 0, bits = 0, o = 0;
        for (int k = 0; k < vl; k++) {
            int d = b64v((unsigned char)v[k]);
            if (d < 0) continue;
            acc = (acc << 6) | d;
            bits += 6;
            if (bits >= 8) { bits -= 8; raw[o++] = (acc >> bits) & 0xFF; }
        }
        take_picture_block(ti, raw, o);
        free(raw);
    }
}
