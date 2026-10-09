/*
 * syntax.h - syntax highlighting: a rule-driven lexer that classifies the
 * bytes of one line, the language definitions it runs (langs.c), and a
 * cache of the lexer state at line starts for a buffer.
 *
 * Text is lexed a line at a time. Constructs that continue past the end of
 * a line (block comments, multi-line strings) are carried in a small state
 * value: the state at the end of one line is the state at the start of the
 * next, and 0 means "outside of everything".
 */
#ifndef CEDIT_SYNTAX_H
#define CEDIT_SYNTAX_H

#include "buffer.h"

/* Highlight classes. The themes map each to a color. */
enum {
    HL_NORMAL, HL_KEYWORD, HL_TYPE, HL_COMMENT, HL_STRING, HL_NUMBER,
    HL_PREPROC, HL_COUNT
};

/* Rule kinds. */
enum {
    R_LINE,         /* open .. end of the line */
    R_SPAN,         /* open .. close, possibly over several lines */
    R_DIRECTIVE,    /* open, blanks and a word, as in "# include" */
    R_WORDS         /* any word of a list */
};

/* Rule flags. */
#define RF_MULTI 0x01   /* a span may continue on the next lines */
#define RF_CONT  0x02   /* ... or only when its line ends in the escape */
#define RF_NEST  0x04   /* an open inside the span opens a nested one */
#define RF_BOL   0x08   /* open and close only after the line's indentation */
#define RF_WORD  0x10   /* open only at the line start or after a blank or
                           one of ;&|() (shell comments, not "$#") */
#define RF_COUNT 0x20   /* the rep character in open repeats any number
                           of times, and must repeat as often in close:
                           Lua's [==[ ]==], Rust's r#" "# (never as the
                           first character of either) */
#define RF_CHAR  0x40   /* a character literal: one character or one escape
                           sequence, then close; otherwise no match (keeps
                           Rust's 'a lifetimes plain) */

typedef struct Rule {
    int kind, cls;          /* R_*, HL_* */
    const char *open;       /* R_LINE, R_SPAN, R_DIRECTIVE */
    const char *close;      /* R_SPAN */
    int esc;                /* R_SPAN: escape character, or 0 */
    int rep;                /* RF_COUNT: the repeated character */
    int flags;              /* RF_* */
    const char *const *words;   /* R_WORDS: NULL-terminated list */
} Rule;

/* Syntax flags. */
#define SYN_NUMBERS  0x01   /* classify numbers */
#define SYN_DIGITSEP 0x02   /* ' separates digits, as in 1'000 (C23, C++) */

typedef struct Syntax Syntax;

/* A lexer: classifies s[0..len) into cls (unless cls is NULL) starting in
 * state, and returns the state at the end of the line. */
typedef unsigned (*LexFn)(const Syntax *syn, unsigned state, const char *s,
                          size_t len, unsigned char *cls);

struct Syntax {
    const char *name;
    const char *files;      /* extensions (".c") and file names
                               ("Makefile"), separated by spaces */
    const char *interp;     /* interpreters named on a "#!" first line */
    const char *word;       /* word characters besides letters, digits, '_'
                               and non-ASCII bytes */
    int esc;                /* escapes the next character outside spans, or 0 */
    int flags;              /* SYN_* */
    const Rule *rules;      /* tried in order at each position */
    int nrules;
    LexFn lex;              /* NULL: syn_lex_rules */

    /* filled in by syn_prepare */
    int ready;
    unsigned char opens[256];   /* a rule's open starts with this byte */
    unsigned char wordc[256];   /* word characters */
};

/* The languages (langs.c). */
extern Syntax syn_langs[];
extern const int syn_nlangs;

/* Builds a Syntax's lookup tables; syn_detect does it for the languages. */
void syn_prepare(Syntax *syn);
/* The language for a file, by its name or else its "#!" first line, or
 * NULL. */
const Syntax *syn_detect(const char *path, const char *line1, size_t len);

/* Lexes one line with the language's lexer (see LexFn). */
unsigned syn_lex(const Syntax *syn, unsigned state, const char *s, size_t len,
                 unsigned char *cls);
/* The rule engine, the lexer of every language without its own. */
unsigned syn_lex_rules(const Syntax *syn, unsigned state, const char *s,
                       size_t len, unsigned char *cls);

/*
 * The lexer states at the start of every HL_STEP-th line of a buffer,
 * computed as far as needed and dropped from the first line an edit
 * changes (Buffer.dirty). A line too far past the known states to lex up to
 * it at once is lexed from a guess, HL_GUESS lines before it outside of
 * everything, while hl_fill works toward it in the background.
 */
typedef struct Highlight {
    const Syntax *syn;      /* NULL: no highlighting */
    Buffer *buf;
    unsigned *ckpt;         /* state at the start of line i * HL_STEP */
    long nckpt, cap;        /* ckpt[0 .. nckpt) are valid */
    unsigned long gen;      /* buffer generation of the memo */
    long memo_ln;           /* the line after the last one lexed by
                               hl_line, and its state; -1 none */
    unsigned memo_state;
    unsigned char *cls;     /* classes of the line hl_line lexed last */
    size_t cls_cap;
} Highlight;

void hl_init(Highlight *h);
void hl_free(Highlight *h);
/* Starts highlighting buffer b as language syn (NULL for none). */
void hl_set(Highlight *h, Buffer *b, const Syntax *syn);
/* The class of each byte of line ln, valid until the next call, or NULL
 * without a language. Lexing may load more of the buffer, so call this
 * before getting the line's text. */
const unsigned char *hl_line(Highlight *h, long ln);
/* Does showing lines up to target still use guessed states? */
int hl_behind(Highlight *h, long target);
/* Lexes about budget more bytes toward line target. */
void hl_fill(Highlight *h, long target, size_t budget);

#endif
