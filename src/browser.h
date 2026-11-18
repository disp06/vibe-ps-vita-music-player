#ifndef VIBE_BROWSER_H
#define VIBE_BROWSER_H

typedef struct { char name[256]; int is_dir; } Entry;

typedef struct {
    char path[512]; /* "" = device list */
    Entry *e;
    int n, cap;
} Browser;

int browser_open(Browser *b, const char *path);
int browser_up(Browser *b); /* returns index of the folder we came from, or 0 */
int browser_enter(Browser *b, int i); /* -1: could not open, stays put */
extern int browser_last_error;
void browser_full(Browser *b, int i, char *out, int max);
/* playable files in this folder; *paths is malloc'd (free with browser_free_list) */
int browser_playlist(Browser *b, int sel, char ***paths, int *start);
void browser_free_list(char **paths, int n);
const char *browser_device_label(const char *name);
/* host preview: inject entries */
void browser_fake(Browser *b, const char *path, const char **names, int n);

#endif
