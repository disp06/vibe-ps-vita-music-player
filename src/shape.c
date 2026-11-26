#include "shape.h"
#include <ctype.h>
#include <string.h>
#include "arabic_tab.h"
#include "font.h"

static const ArabicForm *ar_find(uint32_t cp) {
    for (unsigned i = 0; i < sizeof ar_forms / sizeof *ar_forms; i++)
        if (ar_forms[i].cp == cp) return &ar_forms[i];
    return NULL;
}

static int lam_alef(uint32_t cp) {
    for (unsigned i = 0; i < sizeof ar_lam_alef / sizeof *ar_lam_alef; i++)
        if (ar_lam_alef[i].alef == cp) return (int)i;
    return -1;
}

static int is_mark(uint32_t cp) { return (cp >= 0x064B && cp <= 0x065F) || cp == 0x0670; }
static int joins_fwd(int t) { return t == 'D' || t == 'L' || t == 'C'; }
static int joins_back(int t) { return t == 'D' || t == 'R' || t == 'C'; }

typedef struct { uint32_t cp; int lig; } Item; /* lig: 1 + index into ar_lam_alef */

static int jt_item(const Item *it) {
    if (it->lig) return 'R';
    const ArabicForm *f = ar_find(it->cp);
    if (f) return f->jt;
    return is_mark(it->cp) ? 'T' : 'U';
}

static int shape_arabic(uint32_t *c, int n) {
    Item it[512];
    int m = 0;
    for (int i = 0; i < n; i++) {
        int li = c[i] == 0x0644 && i + 1 < n ? lam_alef(c[i + 1]) : -1;
        if (li >= 0) { it[m].cp = 0x0644; it[m].lig = li + 1; m++; i++; }
        else { it[m].cp = c[i]; it[m].lig = 0; m++; }
    }
    for (int k = 0; k < m; k++) {
        const ArabicForm *f = ar_find(it[k].cp);
        if (!f && !it[k].lig) { c[k] = it[k].cp; continue; }
        int t = jt_item(&it[k]);
        int p = k - 1;
        while (p >= 0 && jt_item(&it[p]) == 'T') p--;
        int q = k + 1;
        while (q < m && jt_item(&it[q]) == 'T') q++;
        int jf = p >= 0 && joins_fwd(jt_item(&it[p])) && joins_back(t);
        int jn = q < m && joins_fwd(t) && joins_back(jt_item(&it[q]));
        if (it[k].lig) {
            const ArabicLigature *l = &ar_lam_alef[it[k].lig - 1];
            c[k] = jf ? l->fin : l->iso;
        } else if (jf && jn) c[k] = f->med;
        else if (jf) c[k] = f->fin;
        else if (jn) c[k] = f->ini;
        else c[k] = f->iso;
    }
    return m;
}

static int is_cons(uint32_t cp) { return (cp >= 0x0915 && cp <= 0x0939) || (cp >= 0x0958 && cp <= 0x095F); }

/* the vowel sign i (U+093F) is written after its consonant but drawn before it */
static void reorder_deva(uint32_t *c, int n) {
    for (int i = 1; i < n; i++) {
        if (c[i] != 0x093F) continue;
        int s = i - 1;
        if (!is_cons(c[s])) continue;
        while (s >= 2 && c[s - 1] == 0x094D && is_cons(c[s - 2])) s -= 2;
        uint32_t m = c[i];
        memmove(c + s + 1, c + s, (size_t)(i - s) * sizeof *c);
        c[s] = m;
    }
}

static int is_rtl(uint32_t cp) {
    if (cp >= 0x0660 && cp <= 0x0669) return 0;
    if (cp >= 0x06F0 && cp <= 0x06F9) return 0;
    return (cp >= 0x0600 && cp <= 0x06FF) || (cp >= 0x0750 && cp <= 0x077F) ||
           (cp >= 0xFB50 && cp <= 0xFDFF) || (cp >= 0xFE70 && cp <= 0xFEFF);
}

static int is_neutral(uint32_t cp) {
    if (cp < 0x80) return !isalnum((int)cp);
    if (cp >= 0x2000 && cp <= 0x206F) return 1;
    return cp == 0x060C || cp == 0x061B || cp == 0x061F || (cp >= 0xA1 && cp <= 0xBF);
}

/* simplified bidi: runs take the direction of their strong characters, neutrals
 * the one before them; right-to-left runs are reversed for left-to-right drawing */
static void reorder_visual(uint32_t *c, int n) {
    static int d[512], rs[512], rl[512], rd[512];
    static uint32_t tmp[512];
    int base = -1, any_rtl = 0;
    for (int i = 0; i < n; i++) {
        if (is_rtl(c[i])) { d[i] = 1; any_rtl = 1; }
        else if (is_neutral(c[i])) d[i] = 0;
        else d[i] = 2;
        if (d[i] && base < 0) base = d[i] == 1;
    }
    if (!any_rtl) return;
    if (base < 0) base = 0;
    int prev = base ? 1 : 2;
    for (int i = 0; i < n; i++) {
        if (d[i]) prev = d[i];
        else d[i] = prev;
    }
    int nr = 0;
    for (int i = 0; i < n;) {
        int j = i;
        while (j < n && d[j] == d[i]) j++;
        rs[nr] = i; rl[nr] = j - i; rd[nr] = d[i]; nr++;
        i = j;
    }
    int o = 0;
    for (int r = 0; r < nr; r++) {
        int k = base ? nr - 1 - r : r;
        if (rd[k] == 1)
            for (int x = rs[k] + rl[k] - 1; x >= rs[k]; x--) tmp[o++] = c[x];
        else
            for (int x = rs[k]; x < rs[k] + rl[k]; x++) tmp[o++] = c[x];
    }
    memcpy(c, tmp, (size_t)o * sizeof *c);
}

int shape_text(const char *s, uint32_t *out, int max) {
    int n = 0, cp;
    const char *p = s;
    while (n < max && (cp = utf8_next(&p))) out[n++] = (uint32_t)cp;
    int ar = 0, dv = 0;
    for (int i = 0; i < n; i++) {
        if (out[i] >= 0x0600 && out[i] <= 0x06FF) ar = 1;
        if (out[i] >= 0x0900 && out[i] <= 0x097F) dv = 1;
    }
    if (dv) reorder_deva(out, n);
    if (ar) n = shape_arabic(out, n);
    reorder_visual(out, n);
    return n;
}
