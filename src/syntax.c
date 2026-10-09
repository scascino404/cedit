/*
 * syntax.c - the rule-driven lexer, language detection, and the cache of
 * lexer states for a buffer.
 */
#include "syntax.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

#define HL_STEP  128                    /* lines between saved states */
#define HL_EXACT ((size_t)4 << 20)      /* lex at most this much at once */
#define HL_GUESS 300                    /* lines lexed from a guessed state */

/* A state: the open span rule (0 for none, else its index + 1), the number
 * of rep characters its open had (RF_COUNT), and its nesting depth. */
#define ST(rule, count, depth) \
    (((unsigned)(rule) + 1) | (unsigned)(count) << 8 | (unsigned)(depth) << 16)
#define ST_RULE(st)  ((int)((st) & 0xFF) - 1)
#define ST_COUNT(st) ((int)((st) >> 8 & 0xFF))
#define ST_DEPTH(st) ((int)((st) >> 16))

static int is_alnum(int c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int is_digit(int c)
{
    return c >= '0' && c <= '9';
}

/* ------------------------------------------------------------------ */
/* the rule engine                                                     */
/* ------------------------------------------------------------------ */

/* Length of pattern p if it is at s[i..len), else 0. */
static size_t match(const char *s, size_t len, size_t i, const char *p)
{
    size_t n = strlen(p);
    return n <= len - i && memcmp(s + i, p, n) == 0 ? n : 0;
}

/* Like match, for a pattern whose first rep character stands for *count
 * repetitions of it, or for any number when *count < 0 (then *count gets
 * the number found). */
static size_t match_count(const char *s, size_t len, size_t i, const char *p,
                          int rep, int *count)
{
    const char *r = strchr(p, rep);
    size_t pre = r ? (size_t)(r - p) : strlen(p), j, n = 0;

    if (pre > len - i || memcmp(s + i, p, pre) != 0)
        return 0;
    j = i + pre;
    if (r) {
        while (j < len && s[j] == rep && (*count < 0 || n < (size_t)*count)) {
            j++;
            n++;
        }
        if (n > 255 || (*count >= 0 && n != (size_t)*count))
            return 0;
        pre = strlen(r + 1);
        if (pre > len - j || memcmp(s + j, r + 1, pre) != 0)
            return 0;
        j += pre;
        *count = (int)n;
    }
    return j - i;
}

static size_t open_at(const Rule *r, const char *s, size_t len, size_t i,
                      int *count)
{
    *count = -1;
    if (r->flags & RF_COUNT)
        return match_count(s, len, i, r->open, r->rep, count);
    *count = 0;
    return match(s, len, i, r->open);
}

static size_t close_at(const Rule *r, const char *s, size_t len, size_t i,
                       int count)
{
    if (r->flags & RF_COUNT)
        return match_count(s, len, i, r->close, r->rep, &count);
    return match(s, len, i, r->close);
}

static void paint(unsigned char *cls, size_t from, size_t to, int c)
{
    if (cls)
        memset(cls + from, c, to - from);
}

/* Lexes the body of span rule ri from s[i] on, up to and including its
 * close. Returns where lexing goes on, and sets *state to the span if it
 * is still open there (at the end of the line). bol is the first byte after
 * the line's indentation. */
static size_t span(const Syntax *syn, int ri, int count, int depth,
                   const char *s, size_t len, size_t i, size_t bol,
                   unsigned *state, unsigned char *cls)
{
    const Rule *r = &syn->rules[ri];
    size_t start = i, n;
    int keep = r->flags & RF_MULTI;
    int open;

    while (i < len) {
        if (r->esc && s[i] == r->esc) {
            if (i + 1 == len) {         /* escaped line end */
                keep |= r->flags & RF_CONT;
                i = len;
                break;
            }
            i += 2;
            continue;
        }
        if (s[i] == r->close[0] && (!(r->flags & RF_BOL) || i == bol) &&
            (n = close_at(r, s, len, i, count)) != 0) {
            i += n;
            if (depth-- == 0) {
                paint(cls, start, i, r->cls);
                *state = 0;
                return i;
            }
            continue;
        }
        if ((r->flags & RF_NEST) && s[i] == r->open[0] &&
            (n = open_at(r, s, len, i, &open)) != 0) {
            i += n;
            depth++;
            continue;
        }
        i++;
    }
    paint(cls, start, len, r->cls);
    *state = keep ? ST(ri, count, depth) : 0;
    return len;
}

/* Matches a character literal of rule r at s[i]: returns its length, or 0. */
static size_t char_literal(const Rule *r, const char *s, size_t len, size_t i)
{
    size_t j = i + strlen(r->open), k, n;

    if (j >= len || match(s, len, j, r->close))
        return 0;
    if (r->esc && s[j] == r->esc) {
        for (k = j + 2; k < len && k < j + 12; k++)
            if ((n = match(s, len, k, r->close)) != 0)
                return k + n - i;
        return 0;
    }
    /* one UTF-8 character */
    k = (unsigned char)s[j] >= 0xF0 ? 4 : (unsigned char)s[j] >= 0xE0 ? 3 :
        (unsigned char)s[j] >= 0xC0 ? 2 : 1;
    j = j + k < len ? j + k : len;
    n = match(s, len, j, r->close);
    return n ? j + n - i : 0;
}

/* Tries the rules at s[*ip]. On a match, lexes what it covers, advances *ip
 * and returns 1. */
static int rule_at(const Syntax *syn, const char *s, size_t len, size_t *ip,
                   size_t bol, unsigned *state, unsigned char *cls)
{
    size_t i = *ip, n, j;
    int k, count;

    for (k = 0; k < syn->nrules; k++) {
        const Rule *r = &syn->rules[k];
        if (r->kind == R_WORDS || s[i] != r->open[0] ||
            ((r->flags & RF_BOL) && i != bol))
            continue;
        if ((r->flags & RF_WORD) && i > 0 && !strchr(" \t;&|()", s[i - 1]))
            continue;
        switch (r->kind) {
        case R_LINE:
            if (!match(s, len, i, r->open))
                continue;
            paint(cls, i, len, r->cls);
            *ip = len;
            return 1;
        case R_SPAN:
            if (r->flags & RF_CHAR) {
                if ((n = char_literal(r, s, len, i)) == 0)
                    continue;
                paint(cls, i, i + n, r->cls);
                *ip = i + n;
                return 1;
            }
            if ((n = open_at(r, s, len, i, &count)) == 0)
                continue;
            paint(cls, i, i + n, r->cls);
            *ip = span(syn, k, count, 0, s, len, i + n, bol, state, cls);
            return 1;
        case R_DIRECTIVE:
            if ((n = match(s, len, i, r->open)) == 0)
                continue;
            for (j = i + n; j < len && (s[j] == ' ' || s[j] == '\t'); j++)
                ;
            while (j < len && syn->wordc[(unsigned char)s[j]])
                j++;
            paint(cls, i, j, r->cls);
            *ip = j;
            return 1;
        }
    }
    return 0;
}

/* The end of the number at s[i]. */
static size_t number(const Syntax *syn, const char *s, size_t len, size_t i)
{
    int hex = s[i] == '0' && i + 1 < len && (s[i + 1] | 0x20) == 'x';
    size_t j = i + 1;

    while (j < len) {
        int c = (unsigned char)s[j], prev = s[j - 1] | 0x20;
        if (is_alnum(c) || c == '_' ||
            (c == '.' && prev != '.' && (j + 1 == len || s[j + 1] != '.')))
            j++;
        else if ((c == '+' || c == '-') && prev == (hex ? 'p' : 'e'))
            j++;
        else if (c == '\'' && (syn->flags & SYN_DIGITSEP) && j + 1 < len &&
                 is_alnum((unsigned char)s[j + 1]))
            j++;
        else
            break;
    }
    return j;
}

/* The slot of word s[0..n) in the hash of the word lists. */
static unsigned word_hash(const char *s, size_t n)
{
    const unsigned char *u = (const unsigned char *)s;
    return (u[0] * 7u + u[n - 1] * 31u + u[n / 2] * 3u + (unsigned)n * 13u) & 255;
}

/* The class of word s[0..n) from the word lists, by their hash (a lexed
 * line has many words; the lists have up to about a hundred). */
static int word_class(const Syntax *syn, const char *s, size_t n)
{
    unsigned h;
    if (n > syn->wmax)
        return HL_NORMAL;
    for (h = word_hash(s, n); syn->wword[h]; h = (h + 1) & 255)
        if (syn->wlen[h] == n && memcmp(syn->wword[h], s, n) == 0)
            return syn->wcls[h];
    return HL_NORMAL;
}

unsigned syn_lex(const Syntax *syn, unsigned state, const char *s, size_t len,
                 unsigned char *cls)
{
    size_t i = 0, bol = 0, j;

    /* Only rules change the state: for the state alone, a line outside of
     * everything with nothing that could open one can be skipped. */
    if (!cls && !state) {
        while (i < len && !syn->opens[(unsigned char)s[i]])
            i++;
        if (i == len)
            return 0;
        i = 0;
    }
    paint(cls, 0, len, HL_NORMAL);
    while (bol < len && (s[bol] == ' ' || s[bol] == '\t'))
        bol++;
    if (state) {
        int ri = ST_RULE(state);
        if (ri >= 0 && ri < syn->nrules && syn->rules[ri].kind == R_SPAN)
            i = span(syn, ri, ST_COUNT(state), ST_DEPTH(state), s, len, 0, bol,
                     &state, cls);
        else
            state = 0;
    }
    while (i < len) {
        int c = (unsigned char)s[i];
        if (syn->opens[c] && rule_at(syn, s, len, &i, bol, &state, cls))
            continue;
        if (c == syn->esc && c) {
            i += i + 1 < len ? 2 : 1;
        } else if ((syn->flags & SYN_NUMBERS) &&
                   (is_digit(c) || (c == '.' && i + 1 < len &&
                                    is_digit((unsigned char)s[i + 1])))) {
            j = number(syn, s, len, i);
            paint(cls, i, j, HL_NUMBER);
            i = j;
        } else if (syn->wordc[c]) {
            for (j = i + 1; j < len && syn->wordc[(unsigned char)s[j]]; j++)
                ;
            if (cls)
                paint(cls, i, j, word_class(syn, s + i, j - i));
            i = j;
        } else {
            i++;
        }
    }
    return state;
}

void syn_prepare(Syntax *syn)
{
    int c, k;

    if (syn->ready)
        return;
    for (c = 0; c < 256; c++)
        syn->wordc[c] = (unsigned char)(is_alnum(c) || c == '_' || c >= 0x80 ||
                                        (c && syn->word && strchr(syn->word, c)));
    for (k = 0; k < syn->nrules; k++)
        if (syn->rules[k].kind != R_WORDS)
            syn->opens[(unsigned char)syn->rules[k].open[0]] = 1;
    /* a word in two lists has the class of the first */
    for (k = 0; k < syn->nrules; k++) {
        const char *const *w;
        if (syn->rules[k].kind != R_WORDS)
            continue;
        for (w = syn->rules[k].words; *w; w++) {
            size_t n = strlen(*w);
            unsigned h = word_hash(*w, n);
            while (syn->wword[h] && !(syn->wlen[h] == n && memcmp(syn->wword[h], *w, n) == 0))
                h = (h + 1) & 255;
            if (syn->wword[h])
                continue;
            syn->wword[h] = *w;
            syn->wlen[h] = (unsigned char)n;
            syn->wcls[h] = (unsigned char)syn->rules[k].cls;
            if (n > syn->wmax)
                syn->wmax = n;
        }
    }
    syn->ready = 1;
}

/* ------------------------------------------------------------------ */
/* language detection                                                  */
/* ------------------------------------------------------------------ */

/* Is name one of the space-separated words in list? With prefix set, a
 * word may also be followed by a version, as "python3.12" is "python". */
static int in_list(const char *list, const char *name, size_t n, int prefix)
{
    while (list && *list) {
        size_t k = strcspn(list, " "), m = k;
        if (prefix)
            while (m < n && (is_digit((unsigned char)name[m]) || name[m] == '.'))
                m++;
        if (k && m == n && k <= n && memcmp(list, name, k) == 0)
            return 1;
        list += k;
        while (*list == ' ')
            list++;
    }
    return 0;
}

/* The program a "#!" line runs, without its directory: the one after
 * "env" (and its options) if that is it. */
static const char *interpreter(const char *s, size_t len, size_t *n)
{
    size_t i = 2, j;

    if (len < 2 || s[0] != '#' || s[1] != '!')
        return NULL;
    for (;;) {
        while (i < len && (s[i] == ' ' || s[i] == '\t'))
            i++;
        for (j = i; j < len && s[j] != ' ' && s[j] != '\t'; j++)
            if (s[j] == '/')
                i = j + 1;
        if (!(j - i == 3 && memcmp(s + i, "env", 3) == 0))
            break;
        do {                            /* skip env's options */
            for (i = j; i < len && (s[i] == ' ' || s[i] == '\t'); i++)
                ;
            for (j = i; j < len && s[j] != ' ' && s[j] != '\t'; j++)
                ;
        } while (i < len && s[i] == '-');
    }
    *n = j - i;
    return j > i ? s + i : NULL;
}

const Syntax *syn_detect(const char *path, const char *line1, size_t len)
{
    const char *base, *ext, *prog = NULL;
    size_t n = 0;
    int k;

    if (path) {
        base = strrchr(path, '/');
        base = base ? base + 1 : path;
        ext = strrchr(base, '.');
        for (k = 0; k < syn_nlangs; k++)
            if (in_list(syn_langs[k].files, base, strlen(base), 0) ||
                (ext && in_list(syn_langs[k].files, ext, strlen(ext), 0))) {
                syn_prepare(&syn_langs[k]);
                return &syn_langs[k];
            }
    }
    if (line1)
        prog = interpreter(line1, len, &n);
    for (k = 0; prog && k < syn_nlangs; k++)
        if (in_list(syn_langs[k].interp, prog, n, 1)) {
            syn_prepare(&syn_langs[k]);
            return &syn_langs[k];
        }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* the state cache                                                     */
/* ------------------------------------------------------------------ */

void hl_init(Highlight *h)
{
    memset(h, 0, sizeof *h);
    h->cap = 64;
    h->ckpt = (unsigned *)xmalloc((size_t)h->cap * sizeof *h->ckpt);
    h->memo_ln = -1;
    h->cls_ln = -1;
}

void hl_free(Highlight *h)
{
    free(h->ckpt);
    free(h->cls);
}

void hl_set(Highlight *h, Buffer *b, const Syntax *syn)
{
    if (h->buf == b && h->syn == syn)
        return;
    h->buf = b;
    h->syn = syn;
    h->ckpt[0] = 0;
    h->nckpt = 1;
    h->memo_ln = -1;
    h->cls_ln = -1;
    b->dirty = -1;
}

/* Drops the states an edit made stale. */
static void sync(Highlight *h)
{
    Buffer *b = h->buf;
    if (b->dirty >= 0) {
        if (h->nckpt > b->dirty / HL_STEP + 1)
            h->nckpt = b->dirty / HL_STEP + 1;
        b->dirty = -1;
    }
    if (h->gen != b->gen) {
        h->gen = b->gen;
        h->memo_ln = -1;
    }
}

/* Lexes from line *ln in state st toward line to, stopping after about
 * budget bytes. With exact set, st is known to be right, and the states at
 * checkpoints are saved. Returns the state at the line *ln stopped at. */
static unsigned run(Highlight *h, long *ln, long to, unsigned st, int exact,
                    size_t budget)
{
    size_t done = 0, len;
    const char *s;

    for (;;) {
        if (exact && *ln % HL_STEP == 0 && *ln / HL_STEP == h->nckpt) {
            if (h->nckpt == h->cap) {
                h->cap *= 2;
                h->ckpt = (unsigned *)xrealloc(h->ckpt,
                                               (size_t)h->cap * sizeof *h->ckpt);
            }
            h->ckpt[h->nckpt++] = st;
        }
        if (*ln >= to || done >= budget)
            return st;
        s = buf_line(h->buf, *ln, &len);
        st = syn_lex(h->syn, st, s, len, NULL);
        done += len + 1;
        ++*ln;
    }
}

/* Bytes from the start of the last known state to the start of line ln. */
static size_t unknown(Highlight *h, long ln)
{
    return buf_line_offset(h->buf, ln) -
           buf_line_offset(h->buf, (h->nckpt - 1) * HL_STEP);
}

/* The state at the start of line ln: exact if a known state is near
 * enough, otherwise guessed. */
static unsigned state_at(Highlight *h, long ln)
{
    long k = ln / HL_STEP, from;

    if (h->memo_ln == ln)
        return h->memo_state;
    if (k >= h->nckpt) {
        k = h->nckpt - 1;
        if (unknown(h, ln) > HL_EXACT && ln - HL_GUESS > k * HL_STEP) {
            from = ln - HL_GUESS;
            return run(h, &from, ln, 0, 0, (size_t)-1);
        }
    }
    from = k * HL_STEP;
    return run(h, &from, ln, h->ckpt[k], 1, (size_t)-1);
}

const unsigned char *hl_line(Highlight *h, long ln)
{
    size_t len;
    unsigned st;
    const char *s;

    if (!h->syn)
        return NULL;
    sync(h);
    /* the same line again (a long one is slow to lex), with the same
     * text and the same states known */
    if (ln == h->cls_ln && h->cls_gen == h->buf->gen && h->cls_nckpt == h->nckpt)
        return h->cls;
    st = state_at(h, ln);
    s = buf_line(h->buf, ln, &len);
    if (len + 1 > h->cls_cap) {
        h->cls_cap = len + 1 + len / 2;
        free(h->cls);
        h->cls = (unsigned char *)xmalloc(h->cls_cap);
    }
    h->memo_state = syn_lex(h->syn, st, s, len, h->cls);
    h->memo_ln = ln + 1;
    h->cls_ln = ln;
    h->cls_gen = h->buf->gen;
    h->cls_nckpt = h->nckpt;
    return h->cls;
}

int hl_behind(Highlight *h, long target)
{
    if (!h->syn)
        return 0;
    sync(h);
    if (target >= buf_lines(h->buf))
        target = buf_lines(h->buf) - 1;
    return target / HL_STEP >= h->nckpt && unknown(h, target) > HL_EXACT;
}

void hl_fill(Highlight *h, long target, size_t budget)
{
    long ln;

    if (!hl_behind(h, target))
        return;
    if (target >= buf_lines(h->buf))
        target = buf_lines(h->buf) - 1;
    ln = (h->nckpt - 1) * HL_STEP;
    run(h, &ln, target, h->ckpt[h->nckpt - 1], 1, budget);
    h->memo_ln = -1;            /* it may have been guessed */
    h->cls_ln = -1;
}
