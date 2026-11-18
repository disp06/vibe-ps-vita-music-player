#include "browser.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "decoder.h"
#ifdef __vita__
#include <psp2/io/stat.h>
#endif

static const char *g_roots[] = {"ux0:", "uma0:", "imc0:", "xmc0:", "ur0:"};

const char *browser_device_label(const char *name) {
    if (!strcmp(name, "ux0:")) return "memory card / sd2vita";
    if (!strcmp(name, "uma0:")) return "usb / sd2vita (uma0)";
    if (!strcmp(name, "imc0:")) return "internal storage";
    if (!strcmp(name, "xmc0:")) return "memory card (xmc0)";
    if (!strcmp(name, "ur0:")) return "system storage";
    return "";
}

static void push(Browser *b, const char *name, int dir) {
    if (b->n == b->cap) {
        b->cap = b->cap ? b->cap * 2 : 64;
        b->e = realloc(b->e, sizeof(Entry) * b->cap);
    }
    snprintf(b->e[b->n].name, sizeof b->e[b->n].name, "%s", name);
    b->e[b->n].is_dir = dir;
    b->n++;
}

static int cmp(const void *pa, const void *pb) {
    const Entry *a = pa, *b = pb;
    if (a->is_dir != b->is_dir) return b->is_dir - a->is_dir;
    const char *x = a->name, *y = b->name;
    for (; *x && *y; x++, y++) {
        /* natural order for track numbers: compare digit runs by value */
        if (isdigit((unsigned char)*x) && isdigit((unsigned char)*y)) {
            long vx = strtol(x, (char **)&x, 10), vy = strtol(y, (char **)&y, 10);
            if (vx != vy) return vx < vy ? -1 : 1;
            x--; y--;
            continue;
        }
        int cx = tolower((unsigned char)*x), cy = tolower((unsigned char)*y);
        if (cx != cy) return cx - cy;
    }
    return (unsigned char)*x - (unsigned char)*y;
}

#ifdef __vita__
#include <psp2/io/dirent.h>
/* Talk to sceIo directly: newlib's opendir() resolves the path and stats it first,
 * which fails on some SD2Vita / exFAT folders that sceIoDopen opens fine. */
static int dopen_any(const char *path) {
    int fd = sceIoDopen(path);
    if (fd >= 0) return fd;
    /* retry with the other spelling: "ux0:/Music" <-> "ux0:Music" */
    char alt[600];
    const char *c = strchr(path, ':');
    if (c && c[1] == '/') snprintf(alt, sizeof alt, "%.*s%s", (int)(c - path + 1), path, c + 2);
    else if (c) snprintf(alt, sizeof alt, "%.*s/%s", (int)(c - path + 1), path, c + 1);
    else return fd;
    int fd2 = sceIoDopen(alt);
    return fd2 >= 0 ? fd2 : fd;
}
static int dir_exists(const char *p) {
    int fd = dopen_any(p);
    if (fd < 0) return 0;
    sceIoDclose(fd);
    return 1;
}
#else
static int dir_exists(const char *p) {
    DIR *d = opendir(p);
    if (!d) return 0;
    closedir(d);
    return 1;
}
#endif

int browser_last_error;

int browser_open(Browser *b, const char *path) {
    b->n = 0;
    snprintf(b->path, sizeof b->path, "%s", path ? path : "");
    if (!b->path[0]) {
        for (unsigned i = 0; i < sizeof g_roots / sizeof *g_roots; i++) {
            char p[16];
            snprintf(p, sizeof p, "%s/", g_roots[i]);
            if (dir_exists(p)) push(b, g_roots[i], 1);
        }
        return 0;
    }
    char p[520];
    snprintf(p, sizeof p, "%s%s", b->path, b->path[strlen(b->path) - 1] == ':' ? "/" : "");
#ifdef __vita__
    int fd = dopen_any(p);
    if (fd < 0) { browser_last_error = fd; return -1; }
    SceIoDirent de;
    for (;;) {
        memset(&de, 0, sizeof de);
        if (sceIoDread(fd, &de) <= 0) break;
        if (de.d_name[0] == '.') continue;
        int isdir = SCE_S_ISDIR(de.d_stat.st_mode);
        if (isdir || decoder_supported(de.d_name)) push(b, de.d_name, isdir);
    }
    sceIoDclose(fd);
#else
    DIR *d = opendir(p);
    if (!d) { browser_last_error = -1; return -1; }
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        char full[1024];
        struct stat st;
        browser_full(b, -1, full, sizeof full);
        strncat(full, de->d_name, sizeof full - strlen(full) - 1);
        int isdir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
        if (isdir || decoder_supported(de->d_name)) push(b, de->d_name, isdir);
    }
    closedir(d);
#endif
    qsort(b->e, b->n, sizeof(Entry), cmp);
    return 0;
}

/* i = -1 gives the folder path with a trailing separator */
void browser_full(Browser *b, int i, char *out, int max) {
    const char *p = b->path;
    size_t n = strlen(p);
    const char *sep = (n && p[n - 1] == ':') ? "/" : (n && p[n - 1] == '/') ? "" : "/";
    if (i < 0) snprintf(out, max, "%s%s", p, sep);
    else snprintf(out, max, "%s%s%s", p, sep, b->e[i].name);
}

int browser_enter(Browser *b, int i) {
    char p[512], old[512];
    if (!b->path[0]) snprintf(p, sizeof p, "%s", b->e[i].name);
    else browser_full(b, i, p, sizeof p);
    snprintf(old, sizeof old, "%s", b->path);
    if (browser_open(b, p) == 0) return 0;
    int err = browser_last_error;
    browser_open(b, old); /* stay in the current folder */
    browser_last_error = err;
    return -1;
}

int browser_up(Browser *b) {
    if (!b->path[0]) return 0;
    char old[512], name[256];
    snprintf(old, sizeof old, "%s", b->path);
    char *s = strrchr(old, '/');
    if (s && s[1] == 0) { *s = 0; s = strrchr(old, '/'); }
    if (!s) { /* "ux0:" -> device list */
        snprintf(name, sizeof name, "%s", old);
        browser_open(b, "");
    } else {
        snprintf(name, sizeof name, "%s", s + 1);
        *s = 0;
        if (old[strlen(old) - 1] == ':') { /* parent is the device root */ }
        browser_open(b, old);
    }
    for (int i = 0; i < b->n; i++) if (!strcmp(b->e[i].name, name)) return i;
    return 0;
}

int browser_playlist(Browser *b, int sel, char ***paths, int *start) {
    int n = 0;
    *paths = malloc(sizeof(char *) * (b->n ? b->n : 1));
    *start = 0;
    for (int i = 0; i < b->n; i++) {
        if (b->e[i].is_dir) continue;
        if (i == sel) *start = n;
        char p[1024];
        browser_full(b, i, p, sizeof p);
        (*paths)[n++] = strdup(p);
    }
    return n;
}

void browser_free_list(char **paths, int n) {
    for (int i = 0; i < n; i++) free(paths[i]);
    free(paths);
}

void browser_fake(Browser *b, const char *path, const char **names, int n) {
    b->n = 0;
    snprintf(b->path, sizeof b->path, "%s", path);
    for (int i = 0; i < n; i++) {
        int dir = names[i][strlen(names[i]) - 1] == '/';
        char nm[256];
        snprintf(nm, sizeof nm, "%s", names[i]);
        if (dir) nm[strlen(nm) - 1] = 0;
        push(b, nm, dir);
    }
}
