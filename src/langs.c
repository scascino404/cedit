/*
 * langs.c - the language definitions for syntax highlighting.
 *
 * Adding a language: write its rules (and word lists) and add a row to
 * syn_langs. Rules are tried in order at each position, so put longer opens
 * first ("\"\"\"" before "\"") and comments before the strings they may
 * contain. See syntax.h for the rule kinds and flags.
 */
#include "syntax.h"

#define NELEM(a) ((int)(sizeof (a) / sizeof (a)[0]))

/* rule constructors */
#define LINE(open, flags, cls)  {R_LINE, cls, open, NULL, 0, 0, flags, NULL}
#define SPAN(open, close, esc, flags, cls) \
                                {R_SPAN, cls, open, close, esc, 0, flags, NULL}
#define COUNTED(open, close, rep, flags, cls) \
                                {R_SPAN, cls, open, close, 0, rep, (flags) | RF_COUNT, NULL}
#define DIRECTIVE(open, cls)    {R_DIRECTIVE, cls, open, NULL, 0, 0, RF_BOL, NULL}
#define WORDS(list, cls)        {R_WORDS, cls, NULL, NULL, 0, 0, 0, list}

/* ---- C and C++ ---------------------------------------------------- */

static const char *const c_keywords[] = {
    "auto", "break", "case", "const", "continue", "default", "do", "else",
    "enum", "extern", "for", "goto", "if", "inline", "register", "restrict",
    "return", "sizeof", "static", "struct", "switch", "typedef", "union",
    "volatile", "while", "_Alignas", "_Alignof", "_Atomic", "_Generic",
    "_Noreturn", "_Static_assert", "_Thread_local", "alignas", "alignof",
    "constexpr", "false", "nullptr", "static_assert", "thread_local", "true",
    "typeof", "typeof_unqual", NULL
};
static const char *const c_types[] = {
    "char", "double", "float", "int", "long", "short", "signed", "unsigned",
    "void", "bool", "_Bool", "_Complex", "_Imaginary", "size_t", "ssize_t",
    "ptrdiff_t", "intptr_t", "uintptr_t", "intmax_t", "uintmax_t", "int8_t",
    "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t",
    "uint64_t", "wchar_t", "off_t", "FILE", "va_list", NULL
};
static const Rule c_rules[] = {
    LINE("//", 0, HL_COMMENT),
    SPAN("/*", "*/", 0, RF_MULTI, HL_COMMENT),
    SPAN("\"", "\"", '\\', RF_CONT, HL_STRING),
    SPAN("'", "'", '\\', RF_CONT, HL_STRING),
    DIRECTIVE("#", HL_PREPROC),
    WORDS(c_keywords, HL_KEYWORD),
    WORDS(c_types, HL_TYPE)
};

static const char *const cpp_keywords[] = {
    "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
    "break", "case", "catch", "class", "co_await", "co_return", "co_yield",
    "compl", "concept", "const", "const_cast", "consteval", "constexpr",
    "constinit", "continue", "decltype", "default", "delete", "do",
    "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false",
    "final", "for", "friend", "goto", "if", "inline", "mutable", "namespace",
    "new", "noexcept", "not", "not_eq", "nullptr", "operator", "or",
    "or_eq", "override", "private", "protected", "public", "register",
    "reinterpret_cast", "requires", "return", "sizeof", "static",
    "static_assert", "static_cast", "struct", "switch", "template", "this",
    "thread_local", "throw", "true", "try", "typedef", "typeid", "typename",
    "union", "using", "virtual", "volatile", "while", "xor", "xor_eq", NULL
};
static const char *const cpp_types[] = {
    "bool", "char", "char8_t", "char16_t", "char32_t", "double", "float",
    "int", "long", "short", "signed", "unsigned", "void", "wchar_t",
    "size_t", "ssize_t", "ptrdiff_t", "intptr_t", "uintptr_t", "int8_t",
    "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t",
    "uint64_t", NULL
};
static const Rule cpp_rules[] = {
    LINE("//", 0, HL_COMMENT),
    SPAN("/*", "*/", 0, RF_MULTI, HL_COMMENT),
    SPAN("R\"(", ")\"", 0, RF_MULTI, HL_STRING),
    SPAN("\"", "\"", '\\', RF_CONT, HL_STRING),
    SPAN("'", "'", '\\', RF_CONT, HL_STRING),
    DIRECTIVE("#", HL_PREPROC),
    WORDS(cpp_keywords, HL_KEYWORD),
    WORDS(cpp_types, HL_TYPE)
};

/* ---- HolyC -------------------------------------------------------- */

/* The TempleOS compiler's keywords, but for those only seen after '#' */
static const char *const holyc_keywords[] = {
    "argpop", "asm", "break", "case", "catch", "class", "default", "do",
    "else", "end", "extern", "_extern", "for", "goto", "haserrcode", "if",
    "import", "_import", "_intern", "interrupt", "lastclass", "lock",
    "noargpop", "noreg", "no_warn", "offset", "public", "reg", "return",
    "sizeof", "start", "static", "switch", "try", "union", "while", "TRUE",
    "FALSE", "NULL", NULL
};
static const char *const holyc_types[] = {
    "U0", "I0", "U8", "I8", "Bool", "U16", "I16", "U32", "I32", "U64", "I64",
    "F64", "U0i", "I0i", "U8i", "I8i", "U16i", "I16i", "U32i", "I32i", "U64i",
    "I64i", "F64i", NULL
};
/* Comments nest, and strings and char constants ('ABC', up to 8 chars)
 * may run over several lines. */
static const Rule holyc_rules[] = {
    LINE("//", 0, HL_COMMENT),
    SPAN("/*", "*/", 0, RF_MULTI | RF_NEST, HL_COMMENT),
    SPAN("\"", "\"", '\\', RF_MULTI, HL_STRING),
    SPAN("'", "'", '\\', RF_MULTI, HL_STRING),
    DIRECTIVE("#", HL_PREPROC),
    WORDS(holyc_keywords, HL_KEYWORD),
    WORDS(holyc_types, HL_TYPE)
};

/* ---- Python ------------------------------------------------------- */

static const char *const py_keywords[] = {
    "False", "None", "True", "and", "as", "assert", "async", "await",
    "break", "class", "continue", "def", "del", "elif", "else", "except",
    "finally", "for", "from", "global", "if", "import", "in", "is",
    "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try",
    "while", "with", "yield", "match", "case", NULL
};
static const char *const py_types[] = {
    "bool", "bytearray", "bytes", "complex", "dict", "float", "frozenset",
    "int", "list", "object", "set", "str", "tuple", NULL
};
static const Rule py_rules[] = {
    LINE("#", 0, HL_COMMENT),
    SPAN("\"\"\"", "\"\"\"", '\\', RF_MULTI, HL_STRING),
    SPAN("'''", "'''", '\\', RF_MULTI, HL_STRING),
    SPAN("\"", "\"", '\\', RF_CONT, HL_STRING),
    SPAN("'", "'", '\\', RF_CONT, HL_STRING),
    WORDS(py_keywords, HL_KEYWORD),
    WORDS(py_types, HL_TYPE)
};

/* ---- Shell and Makefile ------------------------------------------- */

static const char *const sh_keywords[] = {
    "if", "then", "else", "elif", "fi", "case", "esac", "for", "select",
    "while", "until", "do", "done", "in", "function", "time", "return",
    "break", "continue", "exit", "local", "export", "readonly", "declare",
    "typeset", "unset", "shift", "source", "eval", "exec", "trap", NULL
};
static const Rule sh_rules[] = {
    LINE("#", RF_WORD, HL_COMMENT),
    SPAN("'", "'", 0, RF_MULTI, HL_STRING),
    SPAN("\"", "\"", '\\', RF_MULTI, HL_STRING),
    WORDS(sh_keywords, HL_KEYWORD)
};

static const char *const make_keywords[] = {
    "include", "-include", "sinclude", "ifeq", "ifneq", "ifdef", "ifndef",
    "else", "endif", "define", "endef", "export", "unexport", "override",
    "private", "undefine", "vpath", NULL
};
static const Rule make_rules[] = {
    LINE("#", 0, HL_COMMENT),
    WORDS(make_keywords, HL_KEYWORD)
};

/* ---- JavaScript, TypeScript and JSON ------------------------------ */

static const char *const js_keywords[] = {
    "async", "await", "break", "case", "catch", "class", "const",
    "continue", "debugger", "default", "delete", "do", "else", "export",
    "extends", "false", "finally", "for", "function", "if", "import", "in",
    "instanceof", "let", "new", "null", "of", "return", "static", "super",
    "switch", "this", "throw", "true", "try", "typeof", "undefined", "var",
    "void", "while", "with", "yield", NULL
};
static const char *const ts_keywords[] = {
    "abstract", "as", "declare", "enum", "implements", "interface",
    "keyof", "namespace", "private", "protected", "public", "readonly",
    "type", NULL
};
static const char *const ts_types[] = {
    "any", "bigint", "boolean", "never", "number", "object", "string",
    "symbol", "unknown", NULL
};
static const Rule js_rules[] = {
    LINE("//", 0, HL_COMMENT),
    SPAN("/*", "*/", 0, RF_MULTI, HL_COMMENT),
    SPAN("\"", "\"", '\\', RF_CONT, HL_STRING),
    SPAN("'", "'", '\\', RF_CONT, HL_STRING),
    SPAN("`", "`", '\\', RF_MULTI, HL_STRING),
    WORDS(js_keywords, HL_KEYWORD)
};
static const Rule ts_rules[] = {
    LINE("//", 0, HL_COMMENT),
    SPAN("/*", "*/", 0, RF_MULTI, HL_COMMENT),
    SPAN("\"", "\"", '\\', RF_CONT, HL_STRING),
    SPAN("'", "'", '\\', RF_CONT, HL_STRING),
    SPAN("`", "`", '\\', RF_MULTI, HL_STRING),
    WORDS(js_keywords, HL_KEYWORD),
    WORDS(ts_keywords, HL_KEYWORD),
    WORDS(ts_types, HL_TYPE)
};

static const char *const json_keywords[] = {"true", "false", "null", NULL};
static const Rule json_rules[] = {
    SPAN("\"", "\"", '\\', 0, HL_STRING),
    WORDS(json_keywords, HL_KEYWORD)
};

/* ---- Go ----------------------------------------------------------- */

static const char *const go_keywords[] = {
    "break", "case", "chan", "const", "continue", "default", "defer",
    "else", "fallthrough", "for", "func", "go", "goto", "if", "import",
    "interface", "map", "package", "range", "return", "select", "struct",
    "switch", "type", "var", "true", "false", "nil", "iota", NULL
};
static const char *const go_types[] = {
    "any", "bool", "byte", "comparable", "complex64", "complex128", "error",
    "float32", "float64", "int", "int8", "int16", "int32", "int64", "rune",
    "string", "uint", "uint8", "uint16", "uint32", "uint64", "uintptr", NULL
};
static const Rule go_rules[] = {
    LINE("//", 0, HL_COMMENT),
    SPAN("/*", "*/", 0, RF_MULTI, HL_COMMENT),
    SPAN("\"", "\"", '\\', 0, HL_STRING),
    SPAN("'", "'", '\\', 0, HL_STRING),
    SPAN("`", "`", 0, RF_MULTI, HL_STRING),
    WORDS(go_keywords, HL_KEYWORD),
    WORDS(go_types, HL_TYPE)
};

/* ---- Rust --------------------------------------------------------- */

static const char *const rust_keywords[] = {
    "as", "async", "await", "break", "const", "continue", "crate", "dyn",
    "else", "enum", "extern", "false", "fn", "for", "if", "impl", "in",
    "let", "loop", "match", "mod", "move", "mut", "pub", "ref", "return",
    "self", "Self", "static", "struct", "super", "trait", "true", "type",
    "unsafe", "use", "where", "while", NULL
};
static const char *const rust_types[] = {
    "bool", "char", "str", "i8", "i16", "i32", "i64", "i128", "isize", "u8",
    "u16", "u32", "u64", "u128", "usize", "f32", "f64", "String", "Vec",
    "Option", "Result", "Box", NULL
};
static const Rule rust_rules[] = {
    LINE("//", 0, HL_COMMENT),
    SPAN("/*", "*/", 0, RF_MULTI | RF_NEST, HL_COMMENT),
    COUNTED("br#\"", "\"#", '#', RF_MULTI, HL_STRING),
    COUNTED("r#\"", "\"#", '#', RF_MULTI, HL_STRING),
    SPAN("\"", "\"", '\\', RF_MULTI, HL_STRING),
    SPAN("'", "'", '\\', RF_CHAR, HL_STRING),
    WORDS(rust_keywords, HL_KEYWORD),
    WORDS(rust_types, HL_TYPE)
};

/* ---- Lua ---------------------------------------------------------- */

static const char *const lua_keywords[] = {
    "and", "break", "do", "else", "elseif", "end", "false", "for",
    "function", "goto", "if", "in", "local", "nil", "not", "or", "repeat",
    "return", "then", "true", "until", "while", NULL
};
static const Rule lua_rules[] = {
    COUNTED("--[=[", "]=]", '=', RF_MULTI, HL_COMMENT),
    LINE("--", 0, HL_COMMENT),
    COUNTED("[=[", "]=]", '=', RF_MULTI, HL_STRING),
    SPAN("\"", "\"", '\\', RF_CONT, HL_STRING),
    SPAN("'", "'", '\\', RF_CONT, HL_STRING),
    WORDS(lua_keywords, HL_KEYWORD)
};

/* ---- Markdown ----------------------------------------------------- */

static const Rule md_rules[] = {
    SPAN("<!--", "-->", 0, RF_MULTI, HL_COMMENT),
    SPAN("```", "```", 0, RF_MULTI | RF_BOL, HL_STRING),
    SPAN("~~~", "~~~", 0, RF_MULTI | RF_BOL, HL_STRING),
    SPAN("`", "`", 0, 0, HL_STRING),
    LINE("#", RF_BOL, HL_KEYWORD)
};

/* ------------------------------------------------------------------- */

#define LANG(name, files, interp, word, esc, flags, rules) \
    {name, files, interp, word, esc, flags, rules, NELEM(rules), 0, {0}, {0}, {0}, \
     {0}, {0}, 0}

Syntax syn_langs[] = {
    LANG("C", ".c .h", NULL, NULL, 0, SYN_NUMBERS | SYN_DIGITSEP, c_rules),
    LANG("C++", ".cc .cpp .cxx .c++ .hh .hpp .hxx .h++ .ipp", NULL, NULL, 0,
         SYN_NUMBERS | SYN_DIGITSEP, cpp_rules),
    LANG("HolyC", ".HC .HH .hc", NULL, NULL, 0, SYN_NUMBERS, holyc_rules),
    LANG("Python", ".py .pyw .pyi", "python", NULL, 0, SYN_NUMBERS, py_rules),
    LANG("Shell", ".sh .bash .zsh .ksh .bashrc .bash_profile .bash_logout "
         ".profile .zshrc .zprofile .zshenv PKGBUILD",
         "sh bash zsh dash ksh mksh ash", NULL, '\\', 0, sh_rules),
    LANG("Makefile", ".mk .mak Makefile makefile GNUmakefile", "make", "-",
         '\\', 0, make_rules),
    LANG("JavaScript", ".js .mjs .cjs .jsx", "node", "$", 0, SYN_NUMBERS,
         js_rules),
    LANG("TypeScript", ".ts .mts .cts .tsx", NULL, "$", 0, SYN_NUMBERS,
         ts_rules),
    LANG("JSON", ".json", NULL, NULL, 0, SYN_NUMBERS, json_rules),
    LANG("Go", ".go", NULL, NULL, 0, SYN_NUMBERS, go_rules),
    LANG("Rust", ".rs", NULL, NULL, 0, SYN_NUMBERS, rust_rules),
    LANG("Lua", ".lua", "lua luajit", NULL, 0, SYN_NUMBERS, lua_rules),
    LANG("Markdown", ".md .markdown", NULL, NULL, 0, 0, md_rules)
};
const int syn_nlangs = NELEM(syn_langs);
