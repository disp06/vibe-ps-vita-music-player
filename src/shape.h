#ifndef VIBE_SHAPE_H
#define VIBE_SHAPE_H
#include <stdint.h>

/* UTF-8 -> display-order code points: Arabic joining and lam-alef ligatures,
 * Devanagari pre-base vowel sign reordering, right-to-left runs reversed.
 * max must be <= 512. Returns the number of code points written to out. */
int shape_text(const char *s, uint32_t *out, int max);

#endif
