/*
 * silicon.c - compiler for the Silicon language (.sl)
 *
 * Compiles a stack-based, strictly typed, low-level language directly to
 * x86-64 Linux machine code and writes a static ELF executable.
 * No assembler, no linker, no libc in the output.
 *
 *   gcc -O2 -o silicon silicon.c
 *   ./silicon hello.sl -o hello && ./hello
 *
 * Pipeline: lex -> parse -> (type-check + encode, per function) -> ELF
 *
 * C interop (-c): writes a relocatable x86-64 ELF object instead of an executable.
 *   ./silicon -c lib.sl -o lib.o && gcc main.c lib.o -lm
 * - 'extern i32 puts(ptr s)' (in .text) declares a C / other-object function.
 * - 'global' functions and variables are exported; private ones stay local symbols.
 * - Every call follows the System V AMD64 ABI: ints in rdi,rsi,rdx,rcx,r8,r9, f32/f64 in
 *   xmm0-7, the rest on the stack, rsp 16-byte aligned at the call, floats returned in xmm0.
 *   Extern calls set al = number of xmm registers used, so variadic functions like printf
 *   work if you declare the exact argument list you pass.
 * - Data is addressed rip-relative in object mode, so the result links as PIE or -no-pie.
 *
 * Structs: 'section .types:' holds 'struct Name: <type> field ... end' (C-compatible layout, scalar
 * and fixed-array fields). A struct variable ('Name v', 'local Name v', 'Name[4] vs') is a zeroed
 * block used through its address: 'push ptr @v', then 'getf Name.f' / 'setf Name.f' / 'field Name.f'
 * (address of a field), 'index Name' (pointer + i*sizeof) and 'sizeof Name'. Declare .types first.
 *
 * Sections: .data (read+write), .rodata (read-only: a variable there is const, assigning it is a
 * compile error and a store through a pointer faults at run time), .text. In object mode .rodata is
 * a real read-only ELF section; a ptr initialised with @name inside it needs a load-time relocation,
 * which a PIE link accepts with a DT_TEXTREL warning (use -no-pie to avoid it).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/stat.h>

typedef uint64_t u64;
typedef int64_t  i64;

/* ------------------------------------------------------------------ types */
enum { T_VOID, T_I8, T_I16, T_I32, T_I64, T_U8, T_U16, T_U32, T_U64,
       T_F32, T_F64, T_BOOL, T_PTR };
static const char *TYN[] = { "void", "i8", "i16", "i32", "i64", "u8", "u16",
                             "u32", "u64", "f32", "f64", "bool", "ptr" };
static int is_int(int t)    { return t >= T_I8 && t <= T_U64; }
static int is_signed(int t) { return t >= T_I8 && t <= T_I64; }
static int is_float(int t)  { return t == T_F32 || t == T_F64; }
static int ty_size(int t) {
    switch (t) {
    case T_I8: case T_U8: case T_BOOL: return 1;
    case T_I16: case T_U16: return 2;
    case T_I32: case T_U32: case T_F32: return 4;
    default: return 8;
    }
}

static const char *srcname = "";
static const char *fnames[64]; static int nfiles, cur_file;
static void die(int line, const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "%s:%d: error: ", srcname, line);
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

/* ------------------------------------------------------------------ lexer */
enum { K_EOF, K_ID, K_INT, K_FLT, K_STR, K_SYM };
typedef struct {
    int k, line, slen;
    char s[128];
    u64 u;
    double d;
    char *str;
} Tok;

static Tok *toks; static int ntok, ctok, tp;

static void tadd(Tok t) {
    if (ntok == ctok) { ctok = ctok ? ctok * 2 : 4096; toks = realloc(toks, ctok * sizeof(Tok)); }
    toks[ntok++] = t;
}

static void lex(const char *p) {
    int line = 1;
    static const char *two[] = { "==", "!=", ">=", "<=", "&&", "||", "++", "--", 0 };
    for (;;) {
        while (*p) {
            if (*p == '\n') { line++; p++; }
            else if (isspace((unsigned char)*p)) p++;
            else if (*p == ';') { while (*p && *p != '\n') p++; }
            else break;
        }
        Tok t; memset(&t, 0, sizeof t); t.line = line;
        if (!*p) { t.k = K_EOF; tadd(t); return; }
        if (isalpha((unsigned char)*p) || *p == '_' || *p == '.') {
            int n = 0;
            if (*p == '.') t.s[n++] = *p++;
            while (isalnum((unsigned char)*p) || *p == '_') {
                if (n >= 120) die(line, "identifier too long");
                t.s[n++] = *p++;
            }
            if (n == 0 || (n == 1 && t.s[0] == '.')) die(line, "stray '.'");
            t.k = K_ID;
        } else if (isdigit((unsigned char)*p)) {
            char *e;
            if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
                t.u = strtoull(p, &e, 16); t.k = K_INT;
            } else {
                const char *q = p; int isf = 0;
                while (isdigit((unsigned char)*q)) q++;
                if (*q == '.' && isdigit((unsigned char)q[1])) isf = 1;
                if ((*q == 'e' || *q == 'E') &&
                    (isdigit((unsigned char)q[1]) || ((q[1] == '-' || q[1] == '+') && isdigit((unsigned char)q[2])))) isf = 1;
                if (isf) { t.d = strtod(p, &e); t.k = K_FLT; }
                else     { t.u = strtoull(p, &e, 10); t.k = K_INT; }
            }
            if (isalnum((unsigned char)*e) || *e == '_') die(line, "malformed number");
            p = e;
        } else if (*p == '\'' || *p == '"') {
            char q = *p++;
            char *buf = malloc(strlen(p) + 1); int n = 0;
            while (*p != q) {
                if (!*p || *p == '\n') die(line, "unterminated string literal");
                if (*p == '\\') {
                    p++;
                    switch (*p) {
                    case 'n': buf[n++] = '\n'; break;
                    case 't': buf[n++] = '\t'; break;
                    case 'r': buf[n++] = '\r'; break;
                    case '0': buf[n++] = 0; break;
                    case '\\': buf[n++] = '\\'; break;
                    case '\'': buf[n++] = '\''; break;
                    case '"': buf[n++] = '"'; break;
                    case 'x': {
                        int v = 0, c = 0;
                        while (c < 2 && isxdigit((unsigned char)p[1])) {
                            char h = p[1];
                            v = v * 16 + (isdigit((unsigned char)h) ? h - '0' : (tolower(h) - 'a' + 10));
                            p++; c++;
                        }
                        if (!c) die(line, "bad \\x escape");
                        buf[n++] = (char)v; break;
                    }
                    default: die(line, "unknown escape '\\%c'", *p);
                    }
                    p++;
                } else buf[n++] = *p++;
            }
            p++;
            t.k = K_STR; t.str = buf; t.slen = n;
        } else {
            int matched = 0;
            for (int i = 0; two[i]; i++)
                if (p[0] == two[i][0] && p[1] == two[i][1]) {
                    t.s[0] = p[0]; t.s[1] = p[1]; p += 2; matched = 1; break;
                }
            if (!matched) {
                if (!strchr("():,[]{}=@+-*/%!<>", *p)) die(line, "unexpected character '%c'", *p);
                t.s[0] = *p++;
            }
            t.k = K_SYM;
        }
        tadd(t);
    }
}

/* ----------------------------------------------------------------- model */
enum {
    O_PUSH_LIT, O_PUSH_VAR, O_PUSH_ADDR, O_POP_VAR, O_DISCARD, O_INC, O_DEC,
    O_BIN, O_NEG, O_NOT, O_AND, O_OR,
    O_INDEX, O_LOAD, O_STORE, O_CONV, O_FIELD, O_GETF, O_SETF, O_SIZEOF,
    O_CALL, O_RET, O_SYSCALL, O_BR, O_BRIF, O_BREAK, O_CONTINUE,
    O_LABEL, O_END_BLOCK, O_IF, O_END_IF, O_ELSE, O_END_ELSE, O_LOOP, O_END_LOOP
};
enum { CV_SEXT, CV_ZEXT, CV_TRUNC, CV_CAST };
static const char *CVN[] = { "sext", "zext", "trunc", "cast" };
enum { B_ADD, B_SUB, B_MUL, B_DIV, B_MOD, B_EQ, B_NE, B_GT, B_LT, B_GE, B_LE };
static const char *BN[] = { "add", "sub", "mul", "div", "mod", "eq", "ne", "gt", "lt", "ge", "le" };

typedef struct { int op, line, ty, sub, has_else; u64 u; char n1[64], n2[64]; } In;
static In *ins; static int nins, cins;
static void iadd(In in) {
    if (nins == cins) { cins = cins ? cins * 2 : 4096; ins = realloc(ins, cins * sizeof(In)); }
    ins[nins++] = in;
}

typedef struct { char name[64]; int ty; int hasinit; u64 init; int isarr; long count; int off; } Var;
#define MAXV 128
typedef struct {
    char name[64]; int global, noret, ret, np, nv, nblocks, line, i0, i1, file;
    int ext;                      /* extern declaration (no body, defined in C or another object) */
    int pcls[32], pidx[32], nstk; /* System V parameter classes: 0 int reg, 1 xmm reg, 2 stack */
    Var vars[MAXV];
    int addr;
} Func;
static Func fn[1024]; static int nfn;

typedef struct {
    char name[64]; int ty, isarr, global, line, init, off, size, file, ro;
    long count;
    u64 iv; char *str; int slen; char aname[64];
    u64 *vals; int nvals;
} Gv;
static Gv gv[2048]; static int ngv;

static const char *KW[] = {
    "push", "pop", "inc", "dec", "add", "sub", "mul", "div", "mod", "neg", "eq", "ne",
    "gt", "lt", "ge", "le", "not", "and", "or", "call", "ret", "syscall", "br", "brif",
    "if", "else", "loop", "break", "continue", "local", "define", "noreturn", "section", "end",
    "global", "extern", "struct", "field", "getf", "setf", "sizeof", "true", "false", "index", "load", "store", "sext", "zext", "trunc", "cast", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
    "f32", "f64", "bool", "ptr", "void", 0 };
static int is_keyword(const char *s) {
    for (int i = 0; KW[i]; i++) if (!strcmp(s, KW[i])) return 1;
    return 0;
}

/* ----------------------------------------------------------------- parser */
static Tok *pk(void) { return &toks[tp]; }
static Tok *nx(void) { Tok *t = &toks[tp]; if (t->k != K_EOF) tp++; return t; }
static int is_id(const char *s)  { return pk()->k == K_ID  && !strcmp(pk()->s, s); }
static int is_sym(const char *s) { return pk()->k == K_SYM && !strcmp(pk()->s, s); }
static void expect_sym(const char *s) {
    if (!is_sym(s)) die(pk()->line, "expected '%s'", s);
    nx();
}
static const char *ident(const char *what) {
    Tok *t = pk();
    if (t->k != K_ID) die(t->line, "expected %s", what);
    nx();
    return t->s;
}
/* A name clashes if it is already used in the same file, or if either side is global. */
static int name_taken(const char *n, int isglobal) {
    for (int i = 0; i < ngv; i++)
        if (!strcmp(gv[i].name, n) && (gv[i].file == cur_file || gv[i].global || isglobal)) return 1;
    for (int i = 0; i < nfn; i++)
        if (!strcmp(fn[i].name, n) && (fn[i].file == cur_file || fn[i].global || isglobal)) return 1;
    return 0;
}

static int ty_from(const char *s) {
    for (int i = 0; i <= T_PTR; i++) if (!strcmp(s, TYN[i])) return i;
    return -1;
}
static int find_struct(const char *n);
static int parse_type(int allow_void) {
    Tok *t = pk();
    if (t->k != K_ID) die(t->line, "expected a type");
    int ty = ty_from(t->s);
    if (ty < 0 && find_struct(t->s) >= 0) die(t->line, "'%s' is a struct: it has no value type; pass and store it as a ptr (use @name, getf/setf/field)", t->s);
    if (ty < 0) die(t->line, "expected a type, got '%s'", t->s);
    nx();
    if (ty == T_VOID && !allow_void) die(t->line, "'void' is only valid as a return type");
    if (ty != T_PTR && ty != T_VOID && is_id("ptr")) { nx(); ty = T_PTR; } /* "i8 ptr" */
    return ty;
}

static int fits(int ty, int neg, u64 mag) {
    int b = ty_size(ty) * 8;
    if (is_signed(ty)) {
        u64 lim = neg ? (1ULL << (b - 1)) : ((1ULL << (b - 1)) - 1);
        return mag <= lim;
    }
    if (neg) return mag == 0;
    u64 mx = (b == 64) ? ~0ULL : ((1ULL << b) - 1);
    return mag <= mx;
}

/* parse a literal for type ty, return canonical 64-bit value
   (sign-extended for signed types, zero-extended otherwise) */
static u64 parse_lit(int ty, int line) {
    Tok *t = pk(); int neg = 0;
    if (t->k == K_ID && (!strcmp(t->s, "true") || !strcmp(t->s, "false"))) {
        if (ty != T_BOOL) die(line, "'%s' is a bool literal but the type is %s", t->s, TYN[ty]);
        u64 v = (t->s[0] == 't'); nx(); return v;
    }
    if (is_sym("-")) { neg = 1; nx(); t = pk(); }
    if (is_float(ty)) {
        double d;
        if (t->k == K_FLT) d = t->d;
        else if (t->k == K_INT) d = (double)t->u;
        else die(t->line, "expected a literal value");
        if (neg) d = -d;
        nx();
        if (ty == T_F64) { u64 b; memcpy(&b, &d, 8); return b; }
        if (d > 3.4028234663852886e38 || d < -3.4028234663852886e38) die(line, "literal does not fit in f32");
        float f = (float)d; uint32_t b; memcpy(&b, &f, 4); return b;
    }
    if (t->k == K_FLT) die(line, "float literal given for %s; use an integer literal", TYN[ty]);
    if (t->k != K_INT) die(t->line, "expected a literal value");
    if (ty == T_BOOL) die(line, "bool values are 'true' or 'false', not numbers");
    if (ty == T_PTR) {
        if (t->u != 0 || neg) die(line, "the only ptr literal is 0 (null); use @name for addresses");
        nx(); return 0;
    }
    if (!is_int(ty)) die(line, "literal not allowed for type %s", TYN[ty]);
    if (!fits(ty, neg, t->u)) die(line, "literal %s%llu does not fit in %s", neg ? "-" : "", (unsigned long long)t->u, TYN[ty]);
    u64 v = neg ? (0 - t->u) : t->u;
    nx(); return v;
}

/* ------------------------------------------------------------ structs */
typedef struct { char name[64]; int ty, off; long count; } SField;      /* count > 0: inline array field */
typedef struct { char name[64]; int nf, size, align, line, file; SField f[64]; } Struct;
static Struct st[128]; static int nst;
static int find_struct(const char *n) {
    for (int i = 0; i < nst; i++) if (!strcmp(st[i].name, n)) return i;
    return -1;
}
static void parse_structs(void) {          /* body of 'section .types:' (C-compatible layout) */
    while (!is_id("end")) {
        int line = pk()->line;
        if (pk()->k == K_EOF) die(line, "section is missing its 'end'");
        if (!is_id("struct")) die(line, "expected 'struct Name:' (only structs may appear in .types)");
        nx();
        const char *nm = ident("a struct name");
        if (is_keyword(nm) || ty_from(nm) >= 0) die(line, "'%s' is a reserved word", nm);
        if (find_struct(nm) >= 0) die(line, "struct '%s' is already defined", nm);
        if (nst >= 128) die(line, "too many structs");
        Struct *S = &st[nst]; memset(S, 0, sizeof *S); strcpy(S->name, nm); S->line = line; S->file = cur_file;
        expect_sym(":");
        int off = 0, al = 1;
        while (!is_id("end")) {
            int fl = pk()->line;
            if (pk()->k == K_EOF) die(fl, "struct '%s' is missing its 'end'", S->name);
            if (S->nf >= 64) die(fl, "struct '%s' has more than 64 fields", S->name);
            SField *f = &S->f[S->nf];
            f->ty = parse_type(0);
            if (is_sym("[")) {
                nx(); if (pk()->k != K_INT || pk()->u == 0) die(fl, "an array field needs a positive constant length");
                f->count = (long)pk()->u; nx(); expect_sym("]");
            }
            const char *fnm = ident("a field name");
            for (int k = 0; k < S->nf; k++) if (!strcmp(S->f[k].name, fnm)) die(fl, "struct '%s' already has a field '%s'", S->name, fnm);
            strcpy(f->name, fnm);
            int sz = ty_size(f->ty);
            off = (off + sz - 1) / sz * sz; f->off = off;
            off += sz * (f->count ? (int)f->count : 1);
            if (sz > al) al = sz;
            if (off > (1 << 20)) die(fl, "struct '%s' is larger than 1 MiB", S->name);
            S->nf++;
        }
        nx();                                   /* end of struct */
        if (!S->nf) die(line, "struct '%s' has no fields", S->name);
        S->align = al; S->size = (off + al - 1) / al * al;
        nst++;
    }
}
/* 'Name.field' as written in the source: the lexer gives 'Name' and '.field' */
static void parse_field_ref(In *in, int line) {
    strcpy(in->n1, ident("a struct name"));
    Tok *d = pk();
    if (d->k != K_ID || d->s[0] != '.' || !d->s[1]) die(line, "expected a field like '%s.name'", in->n1);
    strcpy(in->n2, d->s + 1); nx();
}
/* a struct-typed variable is a zeroed byte array of the struct's size (or N of them) */
static int parse_struct_vtype(long *count, int line) {
    if (pk()->k != K_ID) return -1;
    int si = find_struct(pk()->s);
    if (si < 0) return -1;
    nx(); *count = st[si].size;
    if (is_sym("[")) {
        nx(); if (pk()->k != K_INT || pk()->u == 0) die(line, "an array of structs needs a positive constant length");
        *count *= (long)pk()->u; nx(); expect_sym("]");
    }
    return si;
}

static void parse_data(int ro) {
    int line = pk()->line;
    Gv *g = &gv[ngv]; memset(g, 0, sizeof *g); g->line = line; g->ro = ro;
    if (is_id("global")) { nx(); g->global = 1; }
    long scount = 0; int si = parse_struct_vtype(&scount, line);
    if (si >= 0) { g->ty = T_U8; g->isarr = 1; g->count = scount; }
    else {
    if (pk()->k == K_ID && ty_from(pk()->s) < 0 && !is_keyword(pk()->s) && (pk() + 1)->k == K_ID)
        die(line, "unknown type '%s' (declare structs in a 'section .types:' before they are used)", pk()->s);
    g->ty = parse_type(0);
    g->count = 0;
    if (is_sym("[")) {
        nx(); g->isarr = 1;
        if (pk()->k == K_INT) { g->count = (long)pk()->u; nx(); } else g->count = -1;
        expect_sym("]");
    }
    }
    const char *nm = ident("a variable name");
    if (is_keyword(nm)) die(line, "'%s' is a reserved word", nm);
    if (name_taken(nm, g->global)) die(line, "'%s' is already defined (global names must be unique across files)", nm);
    strcpy(g->name, nm); g->file = cur_file;
    if (is_sym("=") && si >= 0) die(line, "struct variable '%s' starts zeroed; initial values are not supported on structs", g->name);
    if (is_sym("=")) {
        nx();
        Tok *t = pk();
        if (t->k == K_STR) {
            if (!g->isarr || (g->ty != T_I8 && g->ty != T_U8))
                die(line, "a string literal can only initialize an i8[] or u8[] array");
            if (g->count == -1) g->count = t->slen;
            if (g->count != t->slen)
                die(line, "array '%s' has length %ld but the string is %d bytes", g->name, g->count, t->slen);
            g->init = 2; g->str = t->str; g->slen = t->slen; nx();
        } else if (is_sym("{")) {
            if (!g->isarr) die(line, "'{...}' can only initialize an array");
            nx();
            int cap = 16, n = 0; u64 *vals = malloc(cap * sizeof(u64));
            if (!is_sym("}")) {
                for (;;) {
                    if (n == cap) { cap *= 2; vals = realloc(vals, cap * sizeof(u64)); }
                    vals[n++] = parse_lit(g->ty, line);
                    if (is_sym(",")) { nx(); if (is_sym("}")) break; continue; }
                    break;
                }
            }
            expect_sym("}");
            if (g->count == -1) g->count = n;
            if (g->count != n)
                die(line, "array '%s' has length %ld but the initializer has %d element%s", g->name, g->count, n, n == 1 ? "" : "s");
            g->init = 4; g->vals = vals; g->nvals = n;
        } else if (is_sym("@")) {
            nx();
            if (g->isarr || g->ty != T_PTR) die(line, "only a ptr variable can be initialized with @name");
            strcpy(g->aname, ident("a name after '@'")); g->init = 3;
        } else {
            if (g->isarr) die(line, "an array needs a string or a {...} initializer");
            g->iv = parse_lit(g->ty, line); g->init = 1;
        }
    }
    if (g->isarr && g->count <= 0) die(line, "array '%s' needs a positive length", g->name);
    if (ro && !g->init) die(line, "'%s' is in .rodata, so it needs an initial value (it can never be assigned)", g->name);
    ngv++;
}

static void add_var(Func *f, const char *name, int ty, int hasinit, u64 init, int line) {
    if (is_keyword(name)) die(line, "'%s' is a reserved word", name);
    for (int i = 0; i < f->nv; i++)
        if (!strcmp(f->vars[i].name, name)) die(line, "'%s' is already declared in function '%s'", name, f->name);
    if (f->nv >= MAXV) die(line, "too many variables in '%s'", f->name);
    Var *v = &f->vars[f->nv++];
    strcpy(v->name, name); v->ty = ty; v->hasinit = hasinit; v->init = init;
}

static void emit_simple(int op, int line) { In in; memset(&in, 0, sizeof in); in.op = op; in.line = line; iadd(in); }

static void parse_operand(int ty, int line) {
    In in; memset(&in, 0, sizeof in); in.line = line; in.ty = ty;
    Tok *t = pk();
    if (is_sym("@")) {
        nx();
        if (ty != T_PTR) die(line, "'@name' is a ptr, but the type given is %s", TYN[ty]);
        strcpy(in.n1, ident("a name after '@'")); in.op = O_PUSH_ADDR;
    } else if (t->k == K_INT || t->k == K_FLT || is_sym("-") ||
               (t->k == K_ID && (!strcmp(t->s, "true") || !strcmp(t->s, "false")))) {
        in.u = parse_lit(ty, line); in.op = O_PUSH_LIT;
    } else if (t->k == K_ID) {
        if (is_keyword(t->s)) die(t->line, "expected a value or variable, got '%s'", t->s);
        strcpy(in.n1, t->s); in.op = O_PUSH_VAR; nx();
    } else die(t->line, "expected a value, a variable, or @name");
    iadd(in);
}

static const struct { const char *name, *sym; int op, sub; } OPS[] = {
    {"add", "+", O_BIN, B_ADD}, {"sub", "-", O_BIN, B_SUB}, {"mul", "*", O_BIN, B_MUL},
    {"div", "/", O_BIN, B_DIV}, {"mod", "%", O_BIN, B_MOD}, {"eq", "==", O_BIN, B_EQ},
    {"ne", "!=", O_BIN, B_NE},  {"gt", ">", O_BIN, B_GT},   {"lt", "<", O_BIN, B_LT},
    {"ge", ">=", O_BIN, B_GE},  {"le", "<=", O_BIN, B_LE},  {"neg", 0, O_NEG, 0},
    {"not", "!", O_NOT, 0},     {"and", "&&", O_AND, 0},    {"or", "||", O_OR, 0},
    {0, 0, 0, 0}
};

static int tok_is(Tok *t, const char *name, const char *sym) {
    return (t->k == K_ID && name && !strcmp(t->s, name)) ||
           (t->k == K_SYM && sym && !strcmp(t->s, sym));
}

static void parse_instr(void) {
    Tok *t = pk(); int line = t->line;
    In in; memset(&in, 0, sizeof in); in.line = line;

    if (tok_is(t, "push", 0)) {
        nx(); int ty = parse_type(0); parse_operand(ty, line); return;
    }
    if (tok_is(t, "pop", "=")) {
        int sym = (t->k == K_SYM); nx();
        Tok *n = pk();
        if (n->k == K_ID && !is_keyword(n->s)) {
            in.op = O_POP_VAR; strcpy(in.n1, n->s); nx();
        } else {
            if (sym) die(line, "'=' needs a variable name");
            in.op = O_DISCARD;
        }
        iadd(in); return;
    }
    if (tok_is(t, "inc", "++") || tok_is(t, "dec", "--")) {
        in.op = (t->k == K_ID ? t->s[0] == 'i' : t->s[0] == '+') ? O_INC : O_DEC;
        nx();
        strcpy(in.n1, ident("a variable name")); iadd(in); return;
    }
    for (int i = 0; OPS[i].op || OPS[i].name; i++) {
        if (!tok_is(t, OPS[i].name, OPS[i].sym)) continue;
        in.op = OPS[i].op; in.sub = OPS[i].sub; nx();
        if (in.op == O_BIN || in.op == O_NEG) in.ty = parse_type(0);
        iadd(in); return;
    }
    if (tok_is(t, "index", 0) && (t + 1)->k == K_ID && ty_from((t + 1)->s) < 0) {   /* index Struct: ptr + idx * sizeof(Struct) */
        in.op = O_INDEX; nx(); strcpy(in.n1, ident("a struct name")); iadd(in); return;
    }
    if (tok_is(t, "field", 0) || tok_is(t, "getf", 0) || tok_is(t, "setf", 0)) {
        in.op = t->s[0] == 'f' ? O_FIELD : t->s[0] == 'g' ? O_GETF : O_SETF;
        nx(); parse_field_ref(&in, line); iadd(in); return;
    }
    if (tok_is(t, "sizeof", 0)) { in.op = O_SIZEOF; nx(); strcpy(in.n1, ident("a struct name")); iadd(in); return; }
    if (tok_is(t, "index", 0) || tok_is(t, "load", 0) || tok_is(t, "store", 0)) {
        in.op = t->s[0] == 'i' ? O_INDEX : t->s[0] == 'l' ? O_LOAD : O_STORE;
        nx(); in.ty = parse_type(0); iadd(in); return;
    }
    for (int c = 0; c < 4; c++)
        if (tok_is(t, CVN[c], 0)) { in.op = O_CONV; in.sub = c; nx(); in.ty = parse_type(0); iadd(in); return; }
    if (tok_is(t, "call", 0)) {
        nx(); in.op = O_CALL; strcpy(in.n1, ident("a function name"));
        if (is_sym("(")) {
            nx();
            if (!is_sym(")")) {
                for (;;) {
                    int al = pk()->line; int ty = parse_type(0); parse_operand(ty, al);
                    if (is_sym(",")) { nx(); continue; }
                    break;
                }
            }
            expect_sym(")");
        }
        iadd(in); return;
    }
    if (tok_is(t, "ret", 0))      { nx(); in.op = O_RET; iadd(in); return; }
    if (tok_is(t, "break", 0))    { nx(); in.op = O_BREAK; iadd(in); return; }
    if (tok_is(t, "continue", 0)) { nx(); in.op = O_CONTINUE; iadd(in); return; }
    if (tok_is(t, "syscall", 0))  { nx(); in.op = O_SYSCALL; strcpy(in.n1, ident("a syscall name")); iadd(in); return; }
    if (tok_is(t, "br", 0))       { nx(); in.op = O_BR; strcpy(in.n1, ident("a label")); iadd(in); return; }
    if (tok_is(t, "brif", 0)) {
        nx(); in.op = O_BRIF; strcpy(in.n1, ident("a label")); expect_sym(",");
        strcpy(in.n2, ident("a label")); iadd(in); return;
    }
    if (t->k == K_ID) die(line, "unknown instruction '%s'", t->s);
    die(line, "unexpected '%s'", t->k == K_SYM ? t->s : "token");
}

enum { SC_BLOCK, SC_IF, SC_ELSE, SC_LOOP };

static void parse_body(Func *f) {
    int sc[128], nsc = 0;
    for (;;) {
        Tok *t = pk(); int line = t->line;
        if (t->k == K_EOF) die(line, "function '%s' is missing its 'end'", f->name);
        if (is_id("end")) {
            nx();
            if (nsc == 0) {
                if (!f->nblocks) die(line, "function '%s' has no blocks (add 'entry:')", f->name);
                return;
            }
            int k = sc[--nsc];
            emit_simple(k == SC_BLOCK ? O_END_BLOCK : k == SC_IF ? O_END_IF :
                        k == SC_ELSE ? O_END_ELSE : O_END_LOOP, line);
            continue;
        }
        int labeled = (t->k == K_ID && toks[tp + 1].k == K_SYM && !strcmp(toks[tp + 1].s, ":"));
        if (nsc == 0) {
            if (!labeled || is_keyword(t->s))
                die(line, "expected a block label like 'entry:' (instructions must be inside a block)");
            In in; memset(&in, 0, sizeof in); in.op = O_LABEL; in.line = line; strcpy(in.n1, t->s);
            nx(); nx(); iadd(in);
            sc[nsc++] = SC_BLOCK; f->nblocks++;
            continue;
        }
        if (nsc >= 120) die(line, "blocks nested too deeply");
        if (labeled) {
            if (!strcmp(t->s, "if")) { nx(); nx(); emit_simple(O_IF, line); sc[nsc++] = SC_IF; continue; }
            if (!strcmp(t->s, "loop")) { nx(); nx(); emit_simple(O_LOOP, line); sc[nsc++] = SC_LOOP; continue; }
            if (!strcmp(t->s, "else")) {
                if (nins == 0 || ins[nins - 1].op != O_END_IF)
                    die(line, "'else:' must come directly after the 'end' of an 'if:'");
                ins[nins - 1].has_else = 1;
                nx(); nx(); emit_simple(O_ELSE, line); sc[nsc++] = SC_ELSE; continue;
            }
            die(line, "labels cannot be nested inside a block ('%s:')", t->s);
        }
        parse_instr();
    }
}

static void classify(Func *f) {          /* System V: ints -> rdi,rsi,rdx,rcx,r8,r9; floats -> xmm0-7; rest on the stack */
    int ni = 0, nf = 0, ns = 0;
    for (int i = 0; i < f->np; i++) {
        if (is_float(f->vars[i].ty) && nf < 8)  { f->pcls[i] = 1; f->pidx[i] = nf++; }
        else if (!is_float(f->vars[i].ty) && ni < 6) { f->pcls[i] = 0; f->pidx[i] = ni++; }
        else { f->pcls[i] = 2; f->pidx[i] = ns++; }
    }
    f->nstk = ns;
}
static void parse_extern(void) {
    int line = pk()->line; nx();
    Func *f = &fn[nfn]; memset(f, 0, sizeof *f); f->line = line; f->ext = 1; f->global = 1;
    if (is_id("noreturn")) { nx(); f->noret = 1; }
    f->ret = parse_type(1);
    if (f->noret && f->ret != T_VOID) die(line, "a 'noreturn' function must return void");
    const char *nm = ident("a function name");
    if (is_keyword(nm)) die(line, "'%s' is a reserved word", nm);
    strcpy(f->name, nm); f->file = cur_file;
    expect_sym("(");
    if (!is_sym(")")) for (;;) {
        int pl = pk()->line; int ty = parse_type(0);
        add_var(f, ident("a parameter name"), ty, 0, 0, pl); f->np++;
        if (is_sym(",")) { nx(); continue; }
        break;
    }
    expect_sym(")");
    if (f->np > 32) die(line, "'%s' has more than 32 parameters", f->name);
    classify(f);
    for (int i = 0; i < nfn; i++) if (!strcmp(fn[i].name, f->name)) {      /* repeated declarations must agree */
        Func *o = &fn[i]; int same = o->ext && o->ret == f->ret && o->np == f->np && o->noret == f->noret;
        for (int k = 0; same && k < f->np; k++) if (o->vars[k].ty != f->vars[k].ty) same = 0;
        if (!same) die(line, "'%s' is already defined (global names must be unique across files)", f->name);
        return;
    }
    if (name_taken(f->name, 1)) die(line, "'%s' is already defined (global names must be unique across files)", f->name);
    nfn++; f->i0 = f->i1 = nins;
}
static void parse_func(void) {
    int line = pk()->line;
    if (is_id("extern")) { parse_extern(); return; }
    if (!is_id("define")) die(line, "expected 'define' (only functions may appear in .text)");
    nx();
    Func *f = &fn[nfn]; memset(f, 0, sizeof *f); f->line = line;
    if (is_id("global")) { nx(); f->global = 1; }
    if (is_id("noreturn")) { nx(); f->noret = 1; }
    f->ret = parse_type(1);
    if (f->noret && f->ret != T_VOID) die(line, "a 'noreturn' function must return void");
    const char *nm = ident("a function name");
    if (is_keyword(nm)) die(line, "'%s' is a reserved word", nm);
    if (name_taken(nm, f->global)) die(line, "'%s' is already defined (global names must be unique across files)", nm);
    strcpy(f->name, nm); f->file = cur_file;
    if (is_sym("(")) {
        nx();
        if (!is_sym(")")) {
            for (;;) {
                int pl = pk()->line; int ty = parse_type(0);
                add_var(f, ident("a parameter name"), ty, 0, 0, pl); f->np++;
                if (is_sym(",")) { nx(); continue; }
                break;
            }
        }
        expect_sym(")");
        if (f->np > 32) die(line, "'%s' has more than 32 parameters", f->name);
    }
    classify(f);
    expect_sym(":");
    while (is_id("local")) {
        int ll = pk()->line; nx();
        long scount = 0; int lsi = parse_struct_vtype(&scount, ll);
        if (pk()->k == K_ID && lsi < 0 && ty_from(pk()->s) < 0 && !is_keyword(pk()->s) && (pk() + 1)->k == K_ID)
            die(ll, "unknown type '%s' (declare structs in a 'section .types:' before they are used)", pk()->s);
        int ty = lsi >= 0 ? T_U8 : parse_type(0);
        int isarr = lsi >= 0; long count = lsi >= 0 ? scount : 0;
        if (lsi >= 0 && count > (1 << 20)) die(ll, "local struct is larger than 1 MiB");
        if (lsi < 0 && is_sym("[")) {
            nx(); isarr = 1;
            if (pk()->k != K_INT) die(ll, "a local array needs a constant length, like 'local u8[32] buf'");
            count = (long)pk()->u; nx(); expect_sym("]");
            if (count <= 0) die(ll, "a local array needs a positive length");
            if (count * ty_size(ty) > (1 << 20)) die(ll, "local array is larger than 1 MiB");
        }
        const char *vn = ident("a variable name");
        char name[64]; strcpy(name, vn);
        int hasinit = 0; u64 init = 0;
        if (is_sym("=")) {
            if (isarr) die(ll, "local arrays start zeroed; initializers are not supported on them");
            nx(); init = parse_lit(ty, ll); hasinit = 1;
        }
        add_var(f, name, ty, hasinit, init, ll);
        f->vars[f->nv - 1].isarr = isarr; f->vars[f->nv - 1].count = count;
    }
    nfn++;
    f->i0 = nins;
    parse_body(f);
    f->i1 = nins;
}

static void parse_file(void) {
    while (pk()->k != K_EOF) {
        int line = pk()->line;
        if (!is_id("section")) die(line, "expected 'section .data:', '.rodata:', '.types:' or '.text:'");
        nx();
        Tok *s = nx(); int sec;
        if (s->k == K_ID && !strcmp(s->s, ".data")) sec = 0;
        else if (s->k == K_ID && !strcmp(s->s, ".text")) sec = 1;
        else if (s->k == K_ID && !strcmp(s->s, ".rodata")) sec = 2;
        else if (s->k == K_ID && !strcmp(s->s, ".types")) sec = 3;
        else die(line, "unknown section (use .data, .rodata, .types or .text)");
        expect_sym(":");
        while (!is_id("end")) {
            if (pk()->k == K_EOF) die(line, "section is missing its 'end'");
            if (sec == 3) { parse_structs(); break; } if (sec == 1) parse_func(); else parse_data(sec == 2);
        }
        nx();
    }
}

/* ------------------------------------------------------------ machine code */
static uint8_t *code; static int cl_, ccap;
static int keep_unused = 0;                /* -K keeps functions nothing calls */
static int opt = 1;                        /* peephole on by default; -O0 turns it off */
/* Pending pushes: a push is held back so a following pop can become a mov (or vanish). */
enum { PK_RAX, PK_IMM, PK_LOCAL };
typedef struct { int kind; int32_t v; } Pend;
static Pend pend[16]; static int npend;
static void raw(const uint8_t *b, int n) {
    while (cl_ + n > ccap) { ccap = ccap ? ccap * 2 : 65536; code = realloc(code, ccap); }
    memcpy(code + cl_, b, n); cl_ += n;
}
static void flush_pend(void) {
    int n = npend; npend = 0;
    for (int i = 0; i < n; i++) {
        uint8_t b[8];
        switch (pend[i].kind) {
        case PK_RAX:   b[0] = 0x50; raw(b, 1); break;
        case PK_IMM:   b[0] = 0x68; memcpy(b + 1, &pend[i].v, 4); raw(b, 5); break;
        default:       b[0] = 0xFF; b[1] = 0xB5; memcpy(b + 2, &pend[i].v, 4); raw(b, 6); break;
        }
    }
}
#define clen (flush_pend(), cl_)           /* reading the position flushes pending pushes */
static void emitb(const uint8_t *b, int n) { flush_pend(); raw(b, n); }
static void eb(int b);
#define EV(...) do { uint8_t _b[] = { __VA_ARGS__ }; emitb(_b, sizeof _b); } while (0)   /* like E, for non-constant bytes */
#define E(...) do { static const uint8_t _b[] = { __VA_ARGS__ }; emitb(_b, sizeof _b); } while (0)
static void e32(uint32_t v) { uint8_t b[4]; memcpy(b, &v, 4); emitb(b, 4); }
static void e64(u64 v)      { uint8_t b[8]; memcpy(b, &v, 8); emitb(b, 8); }
static void patch32(int pos, int32_t v) { memcpy(code + pos, &v, 4); }
static void queue_push(int kind, int32_t v) {
    if (!opt || npend == 16) { if (npend == 16) flush_pend(); }
    pend[npend].kind = kind; pend[npend].v = v; npend++;
    if (!opt) flush_pend();
}
static void push_rax(void)           { queue_push(PK_RAX, 0); }
static void push_imm(int32_t v)      { queue_push(PK_IMM, v); }
static void push_local(int32_t disp) { queue_push(PK_LOCAL, disp); }
/* pop into register r (0=rax 1=rcx 2=rdx 6=rsi 7=rdi 8..10=r8..r10) */
static void pop_reg(int r) {
    uint8_t b[8]; int rex = 0x48 | (r >= 8 ? 1 : 0);
    if (npend && opt) {
        if (r == 0)   /* writing rax would corrupt a deeper pending "push rax": flush instead */
            for (int j = 0; j < npend - 1; j++) if (pend[j].kind == PK_RAX) goto real;
        Pend p = pend[--npend];
        switch (p.kind) {
        case PK_RAX:
            if (r != 0) { b[0] = (uint8_t)rex; b[1] = 0x89; b[2] = (uint8_t)(0xC0 | (r & 7)); raw(b, 3); }
            break;
        case PK_IMM:
            if (p.v == 0) {                            /* xor r32, r32 (2 bytes) */
                int n = 0; if (r >= 8) b[n++] = 0x45;
                b[n++] = 0x31; b[n++] = (uint8_t)(0xC0 | ((r & 7) << 3) | (r & 7)); raw(b, n);
            } else if (p.v > 0) {                      /* mov r32, imm32 (zero-extends; 5 bytes) */
                int n = 0; if (r >= 8) b[n++] = 0x41;
                b[n++] = (uint8_t)(0xB8 + (r & 7)); memcpy(b + n, &p.v, 4); raw(b, n + 4);
            } else {                                   /* mov r64, imm32 (sign-extended) */
                b[0] = (uint8_t)rex; b[1] = 0xC7; b[2] = (uint8_t)(0xC0 | (r & 7)); memcpy(b + 3, &p.v, 4); raw(b, 7);
            }
            break;
        default:                                       /* mov r, [rbp+disp32] */
            b[0] = (uint8_t)(0x48 | (r >= 8 ? 4 : 0)); b[1] = 0x8B; b[2] = (uint8_t)(0x85 | ((r & 7) << 3));
            memcpy(b + 3, &p.v, 4); raw(b, 7); break;
        }
        return;
    }
real:
    flush_pend();
    if (r >= 8) { b[0] = 0x41; b[1] = (uint8_t)(0x58 + (r & 7)); raw(b, 2); }
    else        { b[0] = (uint8_t)(0x58 + r); raw(b, 1); }
}
static void discard_top(void) {            /* pop and throw away */
    if (npend && opt) { npend--; return; }
    static const uint8_t d[4] = { 0x48, 0x83, 0xC4, 0x08 }; emitb(d, 4);
}
static void patch_to(int pos, int target) { patch32(pos, target - (pos + 4)); }

static int objmode = 0;                    /* -c: write a relocatable object instead of an executable */
typedef struct { int pos, gi; } DRel;     /* code -> data reference (rip-relative, needs R_X86_64_PC32) */
static DRel drel[65536]; static int ndrel;
typedef struct { int off, gi, ro; } PRel;     /* data -> data pointer (R_X86_64_64) */
static PRel prel[4096]; static int nprel;
#define BASE      0x400000u
#define RO_BASE   0x600000u
#define DATA_BASE 0x800000u
static uint8_t *dat, *rdat; static int dlen, rlen;     /* .data and .rodata images */
static u64 data_addr(Gv *g) { return (g->ro ? RO_BASE : DATA_BASE) + g->off; }

typedef struct { int pos, target, line; } CFix;       /* call fixups, global */
static CFix cfix[65536]; static int ncfix;
static int callee_of[65536];

/* per-function state */
static Func *cf; static int scratch_base;
static int ts[1024], sp, terminated;
typedef struct { int kind, jz, jmp, start, nbrk, has_break; int brk[256]; } Sc;
static Sc scs[128]; static int nsc;
typedef struct { char name[64]; int pos; } Lab;
typedef struct { char name[64]; int pos, line; } LFix;
static Lab blabs[256]; static int nlab;
static LFix lfix[1024]; static int nlfix;
static char cur_block[64];
static int pending_jmp, pending_if_term;

static int32_t disp(int i) { return cf->vars[i].off; }

static void tpush(int t) { if (sp >= 1000) die(0, "internal: type stack overflow"); ts[sp++] = t; }
static void need_empty(int line, const char *where) {
    if (sp != 0) die(line, "stack is not empty %s (%d value%s left)", where, sp, sp == 1 ? "" : "s");
}
static void texpect(int want, int line, const char *who) {
    if (sp == 0) die(line, "stack underflow: %s needs a %s but the stack is empty", who, TYN[want]);
    if (ts[sp - 1] != want) die(line, "%s expected %s on top of the stack, got %s", who, TYN[want], TYN[ts[sp - 1]]);
    sp--;
}
static void texpect2(int want, int line, const char *who) {
    if (sp < 2) die(line, "stack underflow: '%s %s' needs two values but the stack has %d", who, TYN[want], sp);
    for (int i = 1; i <= 2; i++)
        if (ts[sp - i] != want)
            die(line, "'%s %s' needs two %s values, but the %s operand is %s", who, TYN[want], TYN[want], i == 1 ? "right" : "left", TYN[ts[sp - i]]);
    sp -= 2;
}

static void emit_push_imm(u64 v) {
    i64 s = (i64)v;
    if (s >= INT32_MIN && s <= INT32_MAX) push_imm((int32_t)s);
    else { E(0x48, 0xB8); e64(v); push_rax(); }
}
/* canonical 64-bit form of v for type ty (what norm() leaves in rax) */
static u64 canon(int ty, u64 v) {
    switch (ty) {
    case T_I8:  return (u64)(i64)(int8_t)v;
    case T_I16: return (u64)(i64)(int16_t)v;
    case T_I32: return (u64)(i64)(int32_t)v;
    case T_U8:  return (uint8_t)v;
    case T_U16: return (uint16_t)v;
    case T_U32: return (uint32_t)v;
    default: return v;
    }
}
/* constant folding: if the two newest pending pushes are immediates, compute the result now.
   Mirrors the run-time code exactly (64-bit op, then canonicalize). Returns 1 if it folded. */
static int try_fold(int ty, int sub) {
    if (!opt || npend < 2 || pend[npend - 1].kind != PK_IMM || pend[npend - 2].kind != PK_IMM) return 0;
    if (is_float(ty)) return 0;
    u64 a = (u64)(i64)pend[npend - 2].v, b = (u64)(i64)pend[npend - 1].v, r = 0;
    int sg = is_signed(ty);
    switch (sub) {
    case B_ADD: r = a + b; break;
    case B_SUB: r = a - b; break;
    case B_MUL: r = a * b; break;
    case B_DIV: case B_MOD:
        if (b == 0) return 0;                        /* keep the run-time trap */
        if (sg) { i64 x = (i64)a, y = (i64)b; r = (u64)(sub == B_DIV ? x / y : x % y); }
        else r = sub == B_DIV ? a / b : a % b;
        break;
    case B_EQ: r = a == b; break;
    case B_NE: r = a != b; break;
    case B_GT: r = sg ? (i64)a >  (i64)b : a >  b; break;
    case B_LT: r = sg ? (i64)a <  (i64)b : a <  b; break;
    case B_GE: r = sg ? (i64)a >= (i64)b : a >= b; break;
    default:   r = sg ? (i64)a <= (i64)b : a <= b; break;
    }
    if (sub < B_EQ) r = canon(ty, r);
    i64 sr = (i64)r;
    if (sr < INT32_MIN || sr > INT32_MAX) return 0;
    npend -= 2; pend[npend].kind = PK_IMM; pend[npend].v = (int32_t)sr; npend++;
    return 1;
}
static void emit_rax_imm(u64 v) {
    i64 s = (i64)v;
    if (s >= INT32_MIN && s <= INT32_MAX) { E(0x48, 0xC7, 0xC0); e32((uint32_t)(int32_t)s); }
    else { E(0x48, 0xB8); e64(v); }
}
/* re-canonicalize rax for type ty (sign/zero extend) */
static void norm(int ty) {
    switch (ty) {
    case T_I8:  E(0x48, 0x0F, 0xBE, 0xC0); break;
    case T_I16: E(0x48, 0x0F, 0xBF, 0xC0); break;
    case T_I32: E(0x48, 0x63, 0xC0); break;
    case T_U8:  E(0x0F, 0xB6, 0xC0); break;
    case T_U16: E(0x0F, 0xB7, 0xC0); break;
    case T_U32: case T_F32: E(0x89, 0xC0); break;
    default: break;
    }
}
static void load_typed(int ty) {          /* rax = *(ty*)rax, canonical */
    switch (ty) {
    case T_I8:  E(0x48, 0x0F, 0xBE, 0x00); break;
    case T_U8: case T_BOOL: E(0x0F, 0xB6, 0x00); break;
    case T_I16: E(0x48, 0x0F, 0xBF, 0x00); break;
    case T_U16: E(0x0F, 0xB7, 0x00); break;
    case T_I32: E(0x48, 0x63, 0x00); break;
    case T_U32: case T_F32: E(0x8B, 0x00); break;
    default:    E(0x48, 0x8B, 0x00); break;
    }
}
static void store_typed(int ty) {         /* *(ty*)rcx = rax */
    switch (ty_size(ty)) {
    case 1: E(0x88, 0x01); break;
    case 2: E(0x66, 0x89, 0x01); break;
    case 4: E(0x89, 0x01); break;
    default: E(0x48, 0x89, 0x01); break;
    }
}

/* float helpers: values travel as raw bits in rax/rcx (f32 in the low 32 bits) */
static void eb(int b) { uint8_t x = (uint8_t)b; emitb(&x, 1); }
static void to_xmm0(int ty) { if (ty == T_F64) E(0x66,0x48,0x0F,0x6E,0xC0); else E(0x66,0x0F,0x6E,0xC0); }
static void to_xmm1(int ty) { if (ty == T_F64) E(0x66,0x48,0x0F,0x6E,0xC9); else E(0x66,0x0F,0x6E,0xC9); }
static void from_xmm0(int ty) { if (ty == T_F64) E(0x66,0x48,0x0F,0x7E,0xC0); else E(0x66,0x0F,0x7E,0xC0); }

static Gv *find_gv(const char *n, int file) {
    Gv *best = 0;
    for (int i = 0; i < ngv; i++) if (!strcmp(gv[i].name, n)) { if (gv[i].file == file) return &gv[i]; if (gv[i].global) best = &gv[i]; }
    return best;
}
static int find_fn(const char *n, int file) {
    int best = -1;
    for (int i = 0; i < nfn; i++) if (!strcmp(fn[i].name, n)) { if (fn[i].file == file) return i; if (fn[i].global) best = i; }
    return best;
}
static void unknown_name(int line, const char *kind, const char *n) {
    for (int i = 0; i < ngv; i++) if (!strcmp(gv[i].name, n))
        die(line, "'%s' is private to %s; declare it 'global' there to use it from here", n, fnames[gv[i].file]);
    for (int i = 0; i < nfn; i++) if (!strcmp(fn[i].name, n))
        die(line, "'%s' is private to %s; declare it 'global' there to use it from here", n, fnames[fn[i].file]);
    die(line, "unknown %s '%s'", kind, n);
}
typedef struct { int local, idx, ty, isarr; Gv *g; } Ref;
static int resolve(const char *n, Ref *r) {
    memset(r, 0, sizeof *r);
    for (int i = 0; i < cf->nv; i++)
        if (!strcmp(cf->vars[i].name, n)) { r->local = 1; r->idx = i; r->ty = cf->vars[i].ty; r->isarr = cf->vars[i].isarr; return 1; }
    Gv *g = find_gv(n, cf->file);
    if (g) { r->g = g; r->ty = g->ty; r->isarr = g->isarr; return 1; }
    return 0;
}
static void lea_data(int rcx, Gv *g) {    /* obj mode: lea rax/rcx,[rip+sym] with a relocation */
    if (ndrel >= 65536) die(0, "too many data references");
    EV(0x48, 0x8D, rcx ? 0x0D : 0x05);
    drel[ndrel].pos = clen; drel[ndrel].gi = (int)(g - gv); ndrel++;
    e32(0);
}
static void load_var(Ref *r) {            /* rax = value of variable */
    if (r->local) { E(0x48, 0x8B, 0x85); e32(disp(r->idx)); }
    else if (objmode) { lea_data(0, r->g); load_typed(r->ty); }
    else { E(0xB8); e32((uint32_t)data_addr(r->g)); load_typed(r->ty); }
}
static void store_var(Ref *r) {           /* variable = rax */
    if (r->local) { E(0x48, 0x89, 0x85); e32(disp(r->idx)); }
    else if (objmode) { lea_data(1, r->g); store_typed(r->ty); }
    else { E(0xB9); e32((uint32_t)data_addr(r->g)); store_typed(r->ty); }
}

static void pop_sys_reg(int i) { static const int r[6] = { 7, 6, 2, 10, 8, 9 }; pop_reg(r[i]); }

typedef struct { const char *name; int num, nargs, args[6]; const char *an; int ret, noret; } Sys;
static const Sys SYS[] = {
    {"read", 0, 3, {T_I32, T_PTR, T_I64}, "fd,buf,count", T_I64, 0},
    {"write", 1, 3, {T_I32, T_PTR, T_I64}, "fd,buf,count", T_I64, 0},
    {"open", 2, 3, {T_PTR, T_I32, T_I32}, "path,flags,mode", T_I64, 0},
    {"close", 3, 1, {T_I32}, "fd", T_I64, 0},
    {"lseek", 8, 3, {T_I32, T_I64, T_I32}, "fd,offset,whence", T_I64, 0},
    {"mmap", 9, 6, {T_PTR, T_I64, T_I32, T_I32, T_I32, T_I64}, "addr,length,prot,flags,fd,offset", T_PTR, 0},
    {"munmap", 11, 2, {T_PTR, T_I64}, "addr,length", T_I64, 0},
    {"brk", 12, 1, {T_PTR}, "addr", T_PTR, 0},
    {"nanosleep", 35, 2, {T_PTR, T_PTR}, "req,rem", T_I64, 0},
    {"getpid", 39, 0, {0}, "", T_I64, 0},
    {"fork", 57, 0, {0}, "", T_I64, 0},
    {"execve", 59, 3, {T_PTR, T_PTR, T_PTR}, "path,argv,envp", T_I64, 0},
    {"exit", 60, 1, {T_I32}, "code", T_VOID, 1},
    {"kill", 62, 2, {T_I32, T_I32}, "pid,sig", T_I64, 0},
    {"getcwd", 79, 2, {T_PTR, T_I64}, "buf,size", T_I64, 0},
    {"mkdir", 83, 2, {T_PTR, T_I32}, "path,mode", T_I64, 0},
    {"unlink", 87, 1, {T_PTR}, "path", T_I64, 0},
    {"exit_group", 231, 1, {T_I32}, "code", T_VOID, 1},
    {0}
};

static void argname(const char *list, int k, char *out) {
    const char *p = list;
    while (k-- > 0) { p = strchr(p, ','); if (!p) { strcpy(out, "?"); return; } p++; }
    int n = 0; while (p[n] && p[n] != ',') { out[n] = p[n]; n++; } out[n] = 0;
}

/* check top n stack entries against types[], then pop them */
static void check_args(int line, const char *what, const int *types, const char *names, int n) {
    if (sp < n) die(line, "%s needs %d argument%s but the stack has %d", what, n, n == 1 ? "" : "s", sp);
    for (int i = 0; i < n; i++) {
        int got = ts[sp - n + i];
        if (got != types[i]) {
            char nm[64]; argname(names, i, nm);
            die(line, "%s: argument %d (%s) expected %s, got %s", what, i + 1, nm, TYN[types[i]], TYN[got]);
        }
    }
    sp -= n;
}

static int new_label(const char *name, int line) {
    for (int i = 0; i < nlab; i++) if (!strcmp(blabs[i].name, name)) die(line, "block '%s' is defined twice in '%s'", name, cf->name);
    if (nlab >= 256) die(line, "too many blocks in '%s'", cf->name);
    strcpy(blabs[nlab].name, name); blabs[nlab].pos = clen; return nlab++;
}
static void jump_label(int opcode2, const char *name, int line) {  /* emits jmp/jcc rel32 to label (fixed later) */
    if (opcode2) { E(0x0F); emitb((uint8_t[]){ (uint8_t)opcode2 }, 1); } else E(0xE9);
    if (nlfix >= 1024) die(line, "too many branches in '%s'", cf->name);
    strcpy(lfix[nlfix].name, name); lfix[nlfix].pos = clen; lfix[nlfix].line = line; nlfix++;
    e32(0);
}
static Sc *find_loop(int line, const char *what) {
    for (int i = nsc - 1; i >= 0; i--) if (scs[i].kind == SC_LOOP) return &scs[i];
    die(line, "'%s' used outside of a 'loop:'", what);
    return 0;
}

static void gen_func(Func *f) {
    if (f->ext) { f->addr = clen; return; }
    cf = f; srcname = fnames[f->file]; f->addr = clen; sp = 0; terminated = 0; nlab = 0; nlfix = 0; nsc = 0;
    pending_jmp = 0; pending_if_term = 0; cur_block[0] = 0;
    int cur = 0;
    for (int i = 0; i < f->nv; i++) {
        Var *v = &f->vars[i];
        if (i < f->np && f->pcls[i] == 2) { v->off = 16 + 8 * f->pidx[i]; continue; }   /* stack parameter: caller's args */
        int sz = v->isarr ? (int)(v->count * ty_size(v->ty)) : 8;
        cur += (sz + 7) & ~7; v->off = -cur;
        if (cur > (1 << 20) + 4096) die(f->line, "stack frame of '%s' is larger than 1 MiB", f->name);
    }
    /* scratch area for calls that pass more than 6 arguments */
    int maxextra = 0;
    for (int ip = f->i0; ip < f->i1; ip++)
        if (ins[ip].op == O_CALL) { int gi = find_fn(ins[ip].n1, f->file); if (gi >= 0 && fn[gi].nstk > maxextra) maxextra = fn[gi].nstk; }
    cur += 8 * maxextra; scratch_base = -cur;
    int frame = (cur + 15) & ~15;
    if (!strcmp(f->name, "_start")) { E(0x48, 0x83, 0xE4, 0xF0); E(0x48, 0x83, 0xEC, 0x08); }  /* kernel entry: align rsp so calls are 16-aligned */
    E(0x55); E(0x48, 0x89, 0xE5);
    if (frame) { E(0x48, 0x81, 0xEC); e32(frame); }
    static const uint8_t pst[6][3] = { {0x48,0x89,0xBD}, {0x48,0x89,0xB5}, {0x48,0x89,0x95},
                                       {0x48,0x89,0x8D}, {0x4C,0x89,0x85}, {0x4C,0x89,0x8D} };
    for (int i = 0; i < f->np; i++) {
        if (f->pcls[i] == 0) {
            emitb(pst[f->pidx[i]], 3); e32(disp(i));
            if (objmode && f->global) {       /* C callers leave the upper bits of narrow ints undefined */
                int t = f->vars[i].ty;
                if (t == T_BOOL || (is_int(t) && ty_size(t) < 8)) {
                    E(0x48, 0x8B, 0x85); e32(disp(i));
                    if (t == T_BOOL) E(0x0F, 0xB6, 0xC0); else norm(t);
                    E(0x48, 0x89, 0x85); e32(disp(i));
                }
            }
        } else if (f->pcls[i] == 1) {         /* movq/movd rax, xmmN ; mov [rbp+d], rax */
            int ty = f->vars[i].ty, x = f->pidx[i];
            if (ty == T_F64) EV(0x66, 0x48, 0x0F, 0x7E, 0xC0 | (x << 3)); else EV(0x66, 0x0F, 0x7E, 0xC0 | (x << 3));
            E(0x48, 0x89, 0x85); e32(disp(i));
        }
    }
    for (int i = f->np; i < f->nv; i++) {
        if (f->vars[i].isarr) {              /* zero the array: lea rdi,[rbp+off]; mov ecx,size; xor eax,eax; rep stosb */
            E(0x48, 0x8D, 0xBD); e32(disp(i));
            E(0xB9); e32((uint32_t)(f->vars[i].count * ty_size(f->vars[i].ty)));
            E(0x31, 0xC0); E(0xF3, 0xAA);
        }
        else if (f->vars[i].hasinit && f->vars[i].init) { emit_rax_imm(f->vars[i].init); E(0x48, 0x89, 0x85); e32(disp(i)); }
        else { E(0x48, 0xC7, 0x85); e32(disp(i)); e32(0); }
    }

    for (int ip = f->i0; ip < f->i1; ip++) {
        In *in = &ins[ip]; int line = in->line;
        int closing = (in->op == O_END_BLOCK || in->op == O_END_IF || in->op == O_END_ELSE || in->op == O_END_LOOP);
        if (terminated && !closing)
            die(line, "unreachable code: the previous instruction ends this block (ret, br, brif, break, continue, syscall exit, a call to a noreturn function, or a loop with no break)");
        switch (in->op) {
        case O_LABEL:
            need_empty(line, "at the start of a block");
            new_label(in->n1, line); strcpy(cur_block, in->n1);
            scs[nsc].kind = SC_BLOCK; nsc++; terminated = 0; break;
        case O_END_BLOCK:
            need_empty(line, "at the end of a block");
            if (!terminated) die(line, "block '%s' ends without a terminator (ret, br, brif, syscall exit, or a call to a noreturn function)", cur_block);
            nsc--; terminated = 0; break;

        case O_PUSH_LIT:
            emit_push_imm(in->u); tpush(in->ty); break;
        case O_PUSH_VAR: {
            Ref r; if (!resolve(in->n1, &r)) unknown_name(line, "variable", in->n1);
            if (r.isarr) die(line, "'%s' is an array; use '@%s' to get its address", in->n1, in->n1);
            if (r.ty != in->ty) die(line, "variable '%s' has type %s, but push says %s", in->n1, TYN[r.ty], TYN[in->ty]);
            if (r.local) push_local(disp(r.idx)); else { load_var(&r); push_rax(); }
            tpush(r.ty); break;
        }
        case O_PUSH_ADDR: {
            Ref r; if (!resolve(in->n1, &r)) unknown_name(line, "name", in->n1);
            if (r.local) { E(0x48, 0x8D, 0x85); e32(disp(r.idx)); push_rax(); }
            else if (objmode) { lea_data(0, r.g); push_rax(); }
            else push_imm((int32_t)data_addr(r.g));
            tpush(T_PTR); break;
        }
        case O_POP_VAR: {
            Ref r; if (!resolve(in->n1, &r)) unknown_name(line, "variable", in->n1);
            if (r.isarr) die(line, "cannot assign to array '%s'", in->n1);
            if (!r.local && r.g->ro) die(line, "'%s' is const (it lives in .rodata) and cannot be assigned", in->n1);
            char who[96]; snprintf(who, sizeof who, "'pop %s'", in->n1);
            texpect(r.ty, line, who);
            pop_reg(0); store_var(&r); break;
        }
        case O_DISCARD:
            if (sp == 0) die(line, "stack underflow: 'pop' on an empty stack");
            sp--; discard_top(); break;
        case O_INC: case O_DEC: {
            Ref r; if (!resolve(in->n1, &r)) unknown_name(line, "variable", in->n1);
            if (!r.local && r.g->ro) die(line, "'%s' is const (it lives in .rodata) and cannot be changed", in->n1);
            if (r.isarr || !is_int(r.ty)) die(line, "'%s' needs an integer variable, '%s' is %s", in->op == O_INC ? "inc" : "dec", in->n1, r.isarr ? "an array" : TYN[r.ty]);
            load_var(&r);
            if (in->op == O_INC) E(0x48, 0x83, 0xC0, 0x01); else E(0x48, 0x83, 0xE8, 0x01);
            norm(r.ty); store_var(&r); break;
        }

        case O_BIN: {
            int ty = in->ty, sub = in->sub; const char *nm = BN[sub];
            if (is_float(ty)) {
                if (sub == B_MOD) die(line, "'mod' does not work on %s", TYN[ty]);
                texpect2(ty, line, nm);
                pop_reg(1); pop_reg(0);                   /* pop rcx ; pop rax */
                int dbl = (ty == T_F64);
                if (sub <= B_DIV) {
                    to_xmm0(ty); to_xmm1(ty);
                    uint8_t op = sub == B_ADD ? 0x58 : sub == B_SUB ? 0x5C : sub == B_MUL ? 0x59 : 0x5E;
                    eb(dbl ? 0xF2 : 0xF3); { uint8_t b[3] = { 0x0F, op, 0xC1 }; emitb(b, 3); }
                    from_xmm0(ty); push_rax(); tpush(ty); break;
                }
                int swap = (sub == B_LT || sub == B_LE);
                if (swap) { to_xmm1(ty); to_xmm0(ty); } else { to_xmm0(ty); to_xmm1(ty); }
                /* ucomis: unordered (NaN) sets ZF=PF=CF=1, so a/ae are false on NaN */
                if (dbl) E(0x66);
                if (swap) E(0x0F, 0x2E, 0xC8); else E(0x0F, 0x2E, 0xC1);
                switch (sub) {
                case B_EQ: E(0x0F, 0x94, 0xC0); E(0x0F, 0x9B, 0xC1); E(0x20, 0xC8); break;  /* sete al; setnp cl; and */
                case B_NE: E(0x0F, 0x95, 0xC0); E(0x0F, 0x9A, 0xC1); E(0x08, 0xC8); break;  /* setne al; setp cl; or */
                case B_GT: case B_LT: E(0x0F, 0x97, 0xC0); break;                           /* seta */
                default:              E(0x0F, 0x93, 0xC0); break;                           /* setae */
                }
                E(0x0F, 0xB6, 0xC0); push_rax(); tpush(T_BOOL); break;
            }
            if (sub <= B_MOD) {
                if (!is_int(ty)) die(line, "'%s' needs an integer type, got %s", nm, TYN[ty]);
            } else if (sub == B_EQ || sub == B_NE) {
                if (!(is_int(ty) || ty == T_BOOL || ty == T_PTR)) die(line, "'%s' does not work on %s", nm, TYN[ty]);
            } else if (!(is_int(ty) || ty == T_PTR)) die(line, "'%s' does not work on %s", nm, TYN[ty]);
            texpect2(ty, line, nm);
            if (try_fold(ty, sub)) { tpush(sub >= B_EQ ? T_BOOL : ty); break; }
            pop_reg(1); pop_reg(0);                       /* pop rcx ; pop rax */
            switch (sub) {
            case B_ADD: E(0x48, 0x01, 0xC8); break;
            case B_SUB: E(0x48, 0x29, 0xC8); break;
            case B_MUL: E(0x48, 0x0F, 0xAF, 0xC1); break;
            case B_DIV: case B_MOD:
                if (is_signed(ty)) { E(0x48, 0x99); E(0x48, 0xF7, 0xF9); }
                else { E(0x31, 0xD2); E(0x48, 0xF7, 0xF1); }
                if (sub == B_MOD) E(0x48, 0x89, 0xD0);
                break;
            default: {
                E(0x48, 0x39, 0xC8);                 /* cmp rax, rcx */
                int sg = is_signed(ty);
                uint8_t cc = 0;
                switch (sub) {
                case B_EQ: cc = 0x94; break;
                case B_NE: cc = 0x95; break;
                case B_GT: cc = sg ? 0x9F : 0x97; break;
                case B_LT: cc = sg ? 0x9C : 0x92; break;
                case B_GE: cc = sg ? 0x9D : 0x93; break;
                case B_LE: cc = sg ? 0x9E : 0x96; break;
                }
                uint8_t b[3] = { 0x0F, cc, 0xC0 }; emitb(b, 3);
                E(0x0F, 0xB6, 0xC0);
            }
            }
            if (sub >= B_EQ) tpush(T_BOOL); else { norm(ty); tpush(ty); }
            push_rax(); break;
        }
        case O_NEG:
            if (!is_signed(in->ty) && !is_float(in->ty)) die(line, "'neg' needs a signed integer or float type, got %s", TYN[in->ty]);
            texpect(in->ty, line, "'neg'");
            if (is_float(in->ty)) {              /* flip the sign bit */
                pop_reg(0);
                if (in->ty == T_F64) { E(0x48, 0xB9); e64(0x8000000000000000ULL); E(0x48, 0x31, 0xC8); }
                else { E(0x35); e32(0x80000000u); }
                push_rax(); tpush(in->ty); break;
            }
            pop_reg(0); E(0x48, 0xF7, 0xD8); norm(in->ty); push_rax(); tpush(in->ty); break;
        case O_NOT:
            texpect(T_BOOL, line, "'not'");
            pop_reg(0); E(0x83, 0xF0, 0x01); push_rax(); tpush(T_BOOL); break;
        case O_AND: case O_OR:
            texpect2(T_BOOL, line, in->op == O_AND ? "and" : "or");
            pop_reg(1); pop_reg(0);
            if (in->op == O_AND) E(0x21, 0xC8); else E(0x09, 0xC8);
            push_rax(); tpush(T_BOOL); break;

        case O_INDEX: {
            /* stack: ..., ptr, idx  ->  ptr + idx*sizeof(T) */
            if (in->n1[0]) {
                int si = find_struct(in->n1); if (si < 0) die(line, "unknown struct '%s'", in->n1);
                if (sp < 2) die(line, "stack underflow: 'index %s' needs a ptr and an integer index but the stack has %d", in->n1, sp);
                if (!is_int(ts[sp - 1])) die(line, "'index %s': the index (top of stack) must be an integer, got %s", in->n1, TYN[ts[sp - 1]]);
                if (ts[sp - 2] != T_PTR) die(line, "'index %s': the value below the index must be a ptr, got %s", in->n1, TYN[ts[sp - 2]]);
                sp -= 2; pop_reg(1); pop_reg(0);
                E(0x48, 0x69, 0xC9); e32(st[si].size); E(0x48, 0x01, 0xC8);
                push_rax(); tpush(T_PTR); break;
            }
            if (sp < 2) die(line, "stack underflow: 'index %s' needs a ptr and an integer index but the stack has %d", TYN[in->ty], sp);
            if (!is_int(ts[sp - 1])) die(line, "'index %s': the index (top of stack) must be an integer, got %s", TYN[in->ty], TYN[ts[sp - 1]]);
            if (ts[sp - 2] != T_PTR) die(line, "'index %s': the value below the index must be a ptr, got %s", TYN[in->ty], TYN[ts[sp - 2]]);
            sp -= 2;
            pop_reg(1); pop_reg(0);                         /* pop rcx (idx) ; pop rax (ptr) */
            if (ty_size(in->ty) != 1) { E(0x48, 0x69, 0xC9); e32(ty_size(in->ty)); }   /* imul rcx, rcx, size */
            E(0x48, 0x01, 0xC8);                      /* add rax, rcx */
            push_rax(); tpush(T_PTR); break;
        }
        case O_SIZEOF: {
            int si = find_struct(in->n1); if (si < 0) die(line, "unknown struct '%s'", in->n1);
            emit_push_imm((u64)st[si].size); tpush(T_U64); break;
        }
        case O_FIELD: case O_GETF: case O_SETF: {
            static const char *nm3[] = { "field", "getf", "setf" }; const char *opn = nm3[in->op - O_FIELD];
            int si = find_struct(in->n1); if (si < 0) die(line, "unknown struct '%s'", in->n1);
            SField *fd = 0; for (int k = 0; k < st[si].nf; k++) if (!strcmp(st[si].f[k].name, in->n2)) fd = &st[si].f[k];
            if (!fd) die(line, "struct '%s' has no field '%s'", in->n1, in->n2);
            if (in->op != O_FIELD && fd->count) die(line, "'%s %s.%s': '%s' is an array field; use 'field' to get its address", opn, in->n1, fd->name, fd->name);
            if (in->op == O_SETF) {
                if (sp < 2) die(line, "stack underflow: 'setf %s.%s' needs a ptr and a value but the stack has %d", in->n1, fd->name, sp);
                if (ts[sp - 1] != fd->ty) die(line, "'setf %s.%s': the value on top of the stack is %s, but the field is %s", in->n1, fd->name, TYN[ts[sp - 1]], TYN[fd->ty]);
                if (ts[sp - 2] != T_PTR) die(line, "'setf %s.%s': the value below it must be a ptr to a %s, got %s", in->n1, fd->name, in->n1, TYN[ts[sp - 2]]);
                sp -= 2; pop_reg(0); pop_reg(1);
                if (fd->off) { E(0x48, 0x81, 0xC1); e32(fd->off); }          /* add rcx, off */
                store_typed(fd->ty); break;
            }
            if (sp == 0) die(line, "stack underflow: '%s %s.%s' needs a ptr but the stack is empty", opn, in->n1, fd->name);
            if (ts[sp - 1] != T_PTR) die(line, "'%s %s.%s' needs a ptr to a %s on top of the stack, got %s", opn, in->n1, fd->name, in->n1, TYN[ts[sp - 1]]);
            sp--; pop_reg(0);
            if (fd->off) { E(0x48, 0x05); e32(fd->off); }                    /* add rax, off */
            if (in->op == O_GETF) { load_typed(fd->ty); push_rax(); tpush(fd->ty); }
            else { push_rax(); tpush(T_PTR); }
            break;
        }
        case O_LOAD:
            if (sp == 0) die(line, "stack underflow: 'load %s' needs a ptr but the stack is empty", TYN[in->ty]);
            if (ts[sp - 1] != T_PTR) die(line, "'load %s' needs a ptr on top of the stack, got %s", TYN[in->ty], TYN[ts[sp - 1]]);
            sp--;
            pop_reg(0); load_typed(in->ty); push_rax(); tpush(in->ty); break;
        case O_STORE: {
            /* stack: ..., ptr, value (top) */
            if (sp < 2) die(line, "stack underflow: 'store %s' needs a ptr and a value but the stack has %d", TYN[in->ty], sp);
            if (ts[sp - 1] != in->ty) die(line, "'store %s': the value on top of the stack is %s, expected %s", TYN[in->ty], TYN[ts[sp - 1]], TYN[in->ty]);
            if (ts[sp - 2] != T_PTR) die(line, "'store %s': the value below it must be a ptr, got %s", TYN[in->ty], TYN[ts[sp - 2]]);
            sp -= 2;
            pop_reg(0); pop_reg(1);                         /* pop rax (value) ; pop rcx (ptr) */
            store_typed(in->ty); break;
        }
        case O_CONV: {
            int to = in->ty, sub = in->sub; const char *nm = CVN[sub];
            if (sp == 0) die(line, "stack underflow: '%s %s' needs a value but the stack is empty", nm, TYN[to]);
            int from = ts[sp - 1];
            if (to == T_BOOL) die(line, "'%s' cannot produce bool; compare with 'ne' instead", nm);
            if (sub == CV_CAST && (is_float(from) || is_float(to))) {
                if (!(is_float(from) || is_int(from)) || !(is_float(to) || is_int(to)))
                    die(line, "'cast %s' cannot convert %s", TYN[to], TYN[from]);
                if (from == to) die(line, "'cast %s': the value is already %s", TYN[to], TYN[to]);
                sp--;
                pop_reg(0);
                if (is_float(from) && is_float(to)) {
                    to_xmm0(from);
                    if (from == T_F32) E(0xF3, 0x0F, 0x5A, 0xC0); else E(0xF2, 0x0F, 0x5A, 0xC0);   /* cvtss2sd / cvtsd2ss */
                    from_xmm0(to);
                } else if (is_float(to) && from == T_U64) {   /* u64 -> float: halve (keeping the low bit) if the top bit is set */
                    uint8_t p = to == T_F64 ? 0xF2 : 0xF3;
                    E(0x48, 0x85, 0xC0); E(0x78, 0x07);                         /* test rax,rax ; js L1 */
                    eb(p); E(0x48, 0x0F, 0x2A, 0xC0); E(0xEB, 0x15);                /* cvtsi2s? xmm0,rax ; jmp done */
                    E(0x48, 0x89, 0xC1); E(0x48, 0xD1, 0xE8); E(0x83, 0xE1, 0x01); E(0x48, 0x09, 0xC8);   /* L1: rcx=rax; shr rax,1; and ecx,1; or rax,rcx */
                    eb(p); E(0x48, 0x0F, 0x2A, 0xC0); eb(p); E(0x0F, 0x58, 0xC0);       /* cvtsi2s? xmm0,rax ; adds? xmm0,xmm0 */
                    from_xmm0(to);
                } else if (is_float(to)) {          /* int -> float; rax is already sign/zero-extended */
                    eb(to == T_F64 ? 0xF2 : 0xF3); E(0x48, 0x0F, 0x2A, 0xC0);   /* cvtsi2sd/ss xmm0, rax */
                    from_xmm0(to);
                } else if (to == T_U64) {           /* float -> u64: values >= 2^63 are converted after subtracting 2^63 */
                    int d = from == T_F64; uint8_t p = d ? 0xF2 : 0xF3;
                    to_xmm0(from);
                    if (d) { E(0x48, 0xB8); e64(0x43E0000000000000ULL); E(0x66, 0x48, 0x0F, 0x6E, 0xC8); }
                    else   { E(0xB8); e32(0x5F000000u); E(0x66, 0x0F, 0x6E, 0xC8); }
                    if (d) E(0x66);
                    E(0x0F, 0x2E, 0xC1);                                        /* ucomis? xmm0,xmm1 */
                    E(0x73, 0x07);                                              /* jae L1 */
                    eb(p); E(0x48, 0x0F, 0x2C, 0xC0); E(0xEB, 0x16);                /* cvtts?2si rax,xmm0 ; jmp done */
                    eb(p); E(0x0F, 0x5C, 0xC1); eb(p); E(0x48, 0x0F, 0x2C, 0xC0);       /* L1: subs? xmm0,xmm1 ; cvtts?2si rax,xmm0 */
                    E(0x48, 0xB9); e64(0x8000000000000000ULL); E(0x48, 0x31, 0xC8);   /* mov rcx,2^63 ; xor rax,rcx */
                } else {                            /* float -> int, truncating toward zero */
                    to_xmm0(from);
                    eb(from == T_F64 ? 0xF2 : 0xF3); E(0x48, 0x0F, 0x2C, 0xC0); /* cvttsd2si/ss rax, xmm0 */
                    norm(to);
                }
                push_rax(); tpush(to); break;
            }
            if (sub == CV_CAST) {
                if (!(is_int(from) || from == T_BOOL || from == T_PTR) || !(is_int(to) || to == T_PTR))
                    die(line, "'cast %s' cannot convert %s", TYN[to], TYN[from]);
            } else {
                if (!is_int(to)) die(line, "'%s' target must be an integer type, got %s", nm, TYN[to]);
                if (is_float(from)) die(line, "'%s' does not work on %s (use 'cast' to convert between floats and integers)", nm, TYN[from]);
                if (sub == CV_SEXT && !is_signed(from)) die(line, "'sext' needs a signed integer on the stack, got %s (use 'zext')", TYN[from]);
                if (sub == CV_ZEXT && !(from == T_BOOL || (is_int(from) && !is_signed(from)))) die(line, "'zext' needs an unsigned integer or bool on the stack, got %s (use 'sext')", TYN[from]);
                if (sub == CV_TRUNC && !is_int(from)) die(line, "'trunc' needs an integer on the stack, got %s", TYN[from]);
                if (sub != CV_TRUNC && ty_size(to) <= ty_size(from)) die(line, "'%s %s' must widen: %s is %d byte%s, %s is %d (use 'trunc' or 'cast')", nm, TYN[to], TYN[from], ty_size(from), ty_size(from) == 1 ? "" : "s", TYN[to], ty_size(to));
                if (sub == CV_TRUNC && ty_size(to) >= ty_size(from)) die(line, "'trunc %s' must narrow: %s is %d byte%s, %s is %d (use 'sext', 'zext' or 'cast')", TYN[to], TYN[from], ty_size(from), ty_size(from) == 1 ? "" : "s", TYN[to], ty_size(to));
            }
            sp--;
            pop_reg(0); norm(to); push_rax(); tpush(to); break;
        }

        case O_IF: {
            texpect(T_BOOL, line, "'if:'");
            need_empty(line, "at 'if:' (it must be the only thing consumed)");
            pop_reg(0); E(0x48, 0x85, 0xC0); E(0x0F, 0x84);
            Sc *s = &scs[nsc++]; memset(s, 0, sizeof *s); s->kind = SC_IF; s->jz = clen; e32(0);
            terminated = 0; break;
        }
        case O_END_IF: {
            need_empty(line, "at the end of 'if:'");
            Sc *s = &scs[--nsc]; int if_term = terminated;
            if (in->has_else) { E(0xE9); pending_jmp = clen; e32(0); pending_if_term = if_term; }
            patch_to(s->jz, clen);
            terminated = 0; break;
        }
        case O_ELSE: {
            Sc *s = &scs[nsc++]; memset(s, 0, sizeof *s); s->kind = SC_ELSE; s->jmp = pending_jmp;
            terminated = 0; break;
        }
        case O_END_ELSE: {
            need_empty(line, "at the end of 'else:'");
            Sc *s = &scs[--nsc]; patch_to(s->jmp, clen);
            terminated = pending_if_term && terminated; break;
        }
        case O_LOOP: {
            need_empty(line, "at 'loop:'");
            Sc *s = &scs[nsc++]; memset(s, 0, sizeof *s); s->kind = SC_LOOP; s->start = clen;
            terminated = 0; break;
        }
        case O_END_LOOP: {
            need_empty(line, "at the end of 'loop:'");
            Sc *s = &scs[--nsc];
            E(0xE9); e32(0); patch_to(clen - 4, s->start);
            for (int i = 0; i < s->nbrk; i++) patch_to(s->brk[i], clen);
            terminated = !s->has_break; break;
        }
        case O_BREAK: {
            Sc *s = find_loop(line, "break"); need_empty(line, "at 'break'");
            if (s->nbrk >= 256) die(line, "too many 'break's in one loop");
            E(0xE9); s->brk[s->nbrk++] = clen; e32(0); s->has_break = 1; terminated = 1; break;
        }
        case O_CONTINUE: {
            Sc *s = find_loop(line, "continue"); need_empty(line, "at 'continue'");
            E(0xE9); e32(0); patch_to(clen - 4, s->start); terminated = 1; break;
        }
        case O_BR:
            need_empty(line, "at 'br'"); jump_label(0, in->n1, line); terminated = 1; break;
        case O_BRIF:
            texpect(T_BOOL, line, "'brif'"); need_empty(line, "at 'brif'");
            pop_reg(0); E(0x48, 0x85, 0xC0);
            jump_label(0x85, in->n1, line);       /* jnz true-label */
            jump_label(0, in->n2, line);          /* jmp false-label */
            terminated = 1; break;

        case O_CALL: {
            int gi = find_fn(in->n1, f->file);
            if (gi < 0) unknown_name(line, "function", in->n1);
            Func *g = &fn[gi];
            int types[32]; char names[2300] = ""; 
            for (int i = 0; i < g->np; i++) { types[i] = g->vars[i].ty; if (i) strcat(names, ","); strcat(names, g->vars[i].name); }
            char what[96]; snprintf(what, sizeof what, "call %s", g->name);
            check_args(line, what, types, names, g->np);
            if (g->ext && !objmode) die(line, "'%s' is declared 'extern'; extern functions need -c (object output) and a C or object-file link", g->name);
            /* System V: pop the arguments (last first) into registers / scratch slots, then align and push stack args */
            for (int i = g->np - 1; i >= 0; i--) {
                if (g->pcls[i] == 2) { pop_reg(0); E(0x48, 0x89, 0x85); e32(scratch_base + 8 * g->pidx[i]); }
                else if (g->pcls[i] == 0) { static const int r[6] = { 7, 6, 2, 1, 8, 9 }; pop_reg(r[g->pidx[i]]); }
                else {
                    pop_reg(0);
                    int x = g->pidx[i];
                    if (g->vars[i].ty == T_F64) EV(0x66, 0x48, 0x0F, 0x6E, 0xC0 | (x << 3)); else EV(0x66, 0x0F, 0x6E, 0xC0 | (x << 3));
                }
            }
            flush_pend();
            int nextra = g->nstk, pad = (sp + nextra) & 1;     /* keep rsp 16-byte aligned at the call */
            if (pad) E(0x48, 0x83, 0xEC, 0x08);
            for (int k = nextra - 1; k >= 0; k--) { E(0x48, 0x8B, 0x85); e32(scratch_base + 8 * k); E(0x50); }
            if (g->ext) {                                       /* al = number of xmm registers used (required by variadic C callees) */
                int nx = 0; for (int i = 0; i < g->np; i++) if (g->pcls[i] == 1) nx++;
                if (nx) { E(0xB8); e32(nx); } else E(0x31, 0xC0);
            }
            E(0xE8);
            if (ncfix >= 65536) die(line, "too many calls");
            cfix[ncfix].pos = clen; cfix[ncfix].line = line; callee_of[ncfix] = gi; ncfix++;
            e32(0);
            if (nextra + pad) { E(0x48, 0x81, 0xC4); e32(8 * (nextra + pad)); }
            if (g->noret) { E(0x0F, 0x0B); terminated = 1; }
            else if (g->ret != T_VOID) {
                if (is_float(g->ret)) from_xmm0(g->ret);
                else if (g->ext) { if (g->ret == T_BOOL) E(0x0F, 0xB6, 0xC0); else norm(g->ret); }   /* C leaves upper bits undefined */
                push_rax(); tpush(g->ret);
            }
            break;
        }
        case O_RET:
            if (f->noret) die(line, "'ret' is not allowed in noreturn function '%s'", f->name);
            if (f->ret != T_VOID) { texpect(f->ret, line, "'ret'"); pop_reg(0); if (is_float(f->ret)) to_xmm0(f->ret); }
            need_empty(line, "at 'ret'");
            E(0xC9, 0xC3); terminated = 1; break;
        case O_SYSCALL: {
            const Sys *s = 0;
            for (int i = 0; SYS[i].name; i++) if (!strcmp(SYS[i].name, in->n1)) s = &SYS[i];
            if (!s) die(line, "unknown syscall '%s'", in->n1);
            char what[96]; snprintf(what, sizeof what, "syscall %s", s->name);
            check_args(line, what, s->args, s->an, s->nargs);
            for (int i = s->nargs - 1; i >= 0; i--) pop_sys_reg(i);
            E(0xB8); e32(s->num); E(0x0F, 0x05);
            if (s->noret) { E(0x0F, 0x0B); terminated = 1; }
            else { push_rax(); tpush(s->ret); }
            break;
        }
        default: die(line, "internal: unhandled op %d", in->op);
        }
    }
    for (int i = 0; i < nlfix; i++) {
        int found = -1;
        for (int j = 0; j < nlab; j++) if (!strcmp(blabs[j].name, lfix[i].name)) found = j;
        if (found < 0) die(lfix[i].line, "unknown block '%s' in function '%s'", lfix[i].name, f->name);
        patch_to(lfix[i].pos, blabs[found].pos);
    }
}

/* ---------------------------------------------------------------- driver */
static char *slurp(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "silicon: cannot open '%s'\n", path); exit(1); }
    fseek(fp, 0, SEEK_END); long n = ftell(fp); fseek(fp, 0, SEEK_SET);
    char *b = malloc(n + 1);
    if (fread(b, 1, n, fp) != (size_t)n) { fprintf(stderr, "silicon: read error\n"); exit(1); }
    b[n] = 0; fclose(fp); return b;
}

static int live[1024], endp[1024], fsize[1024];

static void layout_data(void) {
    int off = 0, roff = 0;
    for (int i = 0; i < ngv; i++) {
        Gv *g = &gv[i];
        g->size = g->isarr ? (int)(g->count * ty_size(g->ty)) : ty_size(g->ty);
        int *o = g->ro ? &roff : &off;
        *o = (*o + 7) & ~7; g->off = *o; *o += g->size;
    }
    dlen = off; dat = calloc(dlen ? dlen : 1, 1);
    rlen = roff; rdat = calloc(rlen ? rlen : 1, 1);
    for (int i = 0; i < ngv; i++) {
        Gv *g = &gv[i]; uint8_t *buf = g->ro ? rdat : dat;
        if (g->init == 1) { u64 v = g->iv; memcpy(buf + g->off, &v, g->size); }
        else if (g->init == 2) memcpy(buf + g->off, g->str, g->slen);
        else if (g->init == 4)
            for (int k = 0; k < g->nvals; k++) memcpy(buf + g->off + k * ty_size(g->ty), &g->vals[k], ty_size(g->ty));
        else if (g->init == 3) {
            Gv *t = find_gv(g->aname, g->file);
            srcname = fnames[g->file];
            if (!t) unknown_name(g->line, "name", g->aname);
            if (objmode) { if (nprel >= 4096) die(g->line, "too many pointer initializers"); prel[nprel].off = g->off; prel[nprel].gi = (int)(t - gv); prel[nprel].ro = g->ro; nprel++; }
            else { u64 a = data_addr(t); memcpy(buf + g->off, &a, 8); }
        }
    }
}

/* ------------------------------------------------- relocatable ELF object (-c) */
typedef struct { uint8_t *p; int n, cap; } Buf;
static void bput(Buf *b, const void *d, int n) {
    while (b->n + n > b->cap) { b->cap = b->cap ? b->cap * 2 : 1024; b->p = realloc(b->p, b->cap); }
    memcpy(b->p + b->n, d, n); b->n += n;
}
static void bu8(Buf *b, int v)  { uint8_t x = (uint8_t)v; bput(b, &x, 1); }
static void bu16(Buf *b, int v) { uint16_t x = (uint16_t)v; bput(b, &x, 2); }
static void bu32(Buf *b, uint32_t v) { bput(b, &v, 4); }
static void bu64(Buf *b, u64 v) { bput(b, &v, 8); }
static int bstr(Buf *b, const char *s) { int at = b->n; bput(b, s, (int)strlen(s) + 1); return at; }
static void bsym(Buf *b, uint32_t name, int bind, int type, int shndx, u64 val, u64 size) {
    bu32(b, name); bu8(b, (bind << 4) | type); bu8(b, 0); bu16(b, shndx); bu64(b, val); bu64(b, size);
}
static void bshdr(Buf *b, uint32_t name, uint32_t type, u64 flags, u64 off, u64 size, uint32_t link, uint32_t info, u64 align, u64 entsz) {
    bu32(b, name); bu32(b, type); bu64(b, flags); bu64(b, 0); bu64(b, off); bu64(b, size);
    bu32(b, link); bu32(b, info); bu64(b, align); bu64(b, entsz);
}
static void write_object(const char *path) {
    enum { S_TEXT = 1, S_DATA, S_RODATA, S_RELT, S_RELD, S_RELR, S_SYMTAB, S_STRTAB, S_SHSTR, S_STACK, NSEC };
    Buf str = {0}, sym = {0}, relt = {0}, reld = {0}, relr = {0}, shs = {0};
    bu8(&str, 0); bu8(&shs, 0);
    static int fsym[1024]; int nsym = 0;
    bsym(&sym, 0, 0, 0, 0, 0, 0); nsym++;                       /* null */
    bsym(&sym, 0, 0, 3, S_TEXT, 0, 0); nsym++;                  /* section symbols: .text = 1, .data = 2 */
    bsym(&sym, 0, 0, 3, S_DATA, 0, 0); nsym++;
    bsym(&sym, 0, 0, 3, S_RODATA, 0, 0); nsym++;               /* .rodata = 3 */
    static int used_ext[1024];
    for (int k = 0; k < ncfix; k++) if (cfix[k].pos >= 0 && fn[callee_of[k]].ext) used_ext[callee_of[k]] = 1;
    int firstglobal;
    for (int pass = 0; pass < 2; pass++) {                      /* locals first, then globals (ELF requires this order) */
        if (pass == 1) firstglobal = nsym;
        for (int i = 0; i < nfn; i++) {
            Func *f = &fn[i]; if (f->ext) continue;
            if (f->addr < 0 || (f->global != pass)) continue;
            bsym(&sym, bstr(&str, f->name), pass, 2, S_TEXT, f->addr, fsize[i]); fsym[i] = nsym++;
        }
        for (int i = 0; i < ngv; i++) {
            Gv *g = &gv[i]; if (g->global != pass) continue;
            bsym(&sym, bstr(&str, g->name), pass, 1, g->ro ? S_RODATA : S_DATA, g->off, g->size); nsym++;
        }
        if (pass == 1) for (int i = 0; i < nfn; i++) if (fn[i].ext && used_ext[i]) {
            bsym(&sym, bstr(&str, fn[i].name), 1, 0, 0, 0, 0); fsym[i] = nsym++;
        }
    }
    if (!firstglobal) firstglobal = nsym;
    for (int k = 0; k < ncfix; k++) {                           /* calls to extern functions: PLT32 against the undefined symbol */
        if (cfix[k].pos < 0 || !fn[callee_of[k]].ext) continue;
        bu64(&relt, cfix[k].pos); bu64(&relt, ((u64)fsym[callee_of[k]] << 32) | 4); bu64(&relt, (u64)(i64)-4);
    }
    for (int k = 0; k < ndrel; k++) {                           /* data references: PC32 against .data + offset */
        if (drel[k].pos < 0) continue;
        bu64(&relt, drel[k].pos); bu64(&relt, ((u64)(gv[drel[k].gi].ro ? 3 : 2) << 32) | 2); bu64(&relt, (u64)(i64)(gv[drel[k].gi].off - 4));
    }
    for (int k = 0; k < nprel; k++) {                           /* ptr initialised with @name: R_X86_64_64 */
        Buf *rb = prel[k].ro ? &relr : &reld;
        bu64(rb, prel[k].off); bu64(rb, ((u64)(gv[prel[k].gi].ro ? 3 : 2) << 32) | 1); bu64(rb, (u64)gv[prel[k].gi].off);
    }
    int n_text = bstr(&shs, ".text"), n_data = bstr(&shs, ".data"), n_relt = bstr(&shs, ".rela.text"), n_reld = bstr(&shs, ".rela.data"), n_ro = bstr(&shs, ".rodata"), n_relr = bstr(&shs, ".rela.rodata");
    int n_sym = bstr(&shs, ".symtab"), n_str = bstr(&shs, ".strtab"), n_shs = bstr(&shs, ".shstrtab"), n_stk = bstr(&shs, ".note.GNU-stack");
    Buf o = {0};
    uint8_t eh[64] = { 0x7f, 'E', 'L', 'F', 2, 1, 1, 0 }; bput(&o, eh, 64);
    #define ALIGN(a) while (o.n % (a)) bu8(&o, 0)
    ALIGN(16); int off_text = o.n; bput(&o, code, clen);
    ALIGN(8);  int off_data = o.n; bput(&o, dat, dlen);
    ALIGN(8);  int off_ro = o.n; bput(&o, rdat, rlen);
    ALIGN(8);  int off_relt = o.n; bput(&o, relt.p, relt.n);
    ALIGN(8);  int off_reld = o.n; bput(&o, reld.p, reld.n);
    ALIGN(8);  int off_relr = o.n; bput(&o, relr.p, relr.n);
    ALIGN(8);  int off_sym  = o.n; bput(&o, sym.p, sym.n);
    int off_str = o.n; bput(&o, str.p, str.n);
    int off_shs = o.n; bput(&o, shs.p, shs.n);
    ALIGN(8);  int off_sh = o.n;
    bshdr(&o, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    bshdr(&o, n_text, 1, 6, off_text, clen, 0, 0, 16, 0);                          /* PROGBITS, AX */
    bshdr(&o, n_data, 1, 3, off_data, dlen, 0, 0, 8, 0);                           /* PROGBITS, WA */
    bshdr(&o, n_ro, 1, 2, off_ro, rlen, 0, 0, 8, 0);                               /* PROGBITS, A (read-only) */
    bshdr(&o, n_relt, 4, 0x40, off_relt, relt.n, S_SYMTAB, S_TEXT, 8, 24);         /* RELA, INFO_LINK */
    bshdr(&o, n_reld, 4, 0x40, off_reld, reld.n, S_SYMTAB, S_DATA, 8, 24);
    bshdr(&o, n_relr, 4, 0x40, off_relr, relr.n, S_SYMTAB, S_RODATA, 8, 24);
    bshdr(&o, n_sym, 2, 0, off_sym, sym.n, S_STRTAB, firstglobal, 8, 24);          /* SYMTAB */
    bshdr(&o, n_str, 3, 0, off_str, str.n, 0, 0, 1, 0);
    bshdr(&o, n_shs, 3, 0, off_shs, shs.n, 0, 0, 1, 0);
    bshdr(&o, n_stk, 1, 0, off_sh, 0, 0, 0, 1, 0);                                 /* empty: stack is not executable */
    uint16_t v16; uint32_t v32; u64 v64;
    v16 = 1;    memcpy(o.p + 16, &v16, 2);         /* ET_REL */
    v16 = 0x3E; memcpy(o.p + 18, &v16, 2);
    v32 = 1;    memcpy(o.p + 20, &v32, 4);
    v64 = off_sh; memcpy(o.p + 40, &v64, 8);       /* shoff */
    v16 = 64;   memcpy(o.p + 52, &v16, 2);
    v16 = 64;   memcpy(o.p + 58, &v16, 2);         /* shentsize */
    v16 = NSEC; memcpy(o.p + 60, &v16, 2);         /* shnum */
    v16 = S_SHSTR; memcpy(o.p + 62, &v16, 2);      /* shstrndx */
    FILE *fp = fopen(path, "wb");
    if (!fp) { fprintf(stderr, "silicon: cannot write '%s'\n", path); exit(1); }
    fwrite(o.p, 1, o.n, fp); fclose(fp);
}

int main(int argc, char **argv) {
    const char *in = 0, *out = 0; const char *inputs[64]; int nin = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "-O0")) opt = 0;
        else if (!strcmp(argv[i], "-O1")) opt = 1;
        else if (!strcmp(argv[i], "-K")) keep_unused = 1;
        else if (!strcmp(argv[i], "-c")) objmode = 1;
        else if (nin < 64) { inputs[nin++] = argv[i]; if (!in) in = argv[i]; }
        else { fprintf(stderr, "silicon: too many input files\n"); return 1; }
    }
    if (!in) { fprintf(stderr, "usage: silicon input.sl [more.sl ...] [-o output] [-c] [-O0|-O1] [-K]\n"); return 1; }
    char outbuf[512];
    if (!out) {
        const char *b = strrchr(in, '/'); b = b ? b + 1 : in;
        snprintf(outbuf, sizeof outbuf, "%s", b);
        char *dot = strrchr(outbuf, '.');
        if (dot) *dot = 0; else strcat(outbuf, ".out");
        if (objmode) strcat(outbuf, ".o");
        out = outbuf;
    }

    int fstart[64];
    for (int i = 0; i < nin; i++) {
        fnames[i] = inputs[i]; srcname = inputs[i]; fstart[i] = ntok;
        lex(slurp(inputs[i]));
    }
    nfiles = nin;
    for (int i = 0; i < nin; i++) {
        cur_file = i; srcname = fnames[i]; tp = fstart[i];
        parse_file();
    }
    Func *start = 0;
    for (int i = 0; i < nfn; i++) if (!strcmp(fn[i].name, "_start")) {
        if (start) { srcname = fnames[fn[i].file]; die(fn[i].line, "'_start' is defined more than once (also in %s)", fnames[start->file]); }
        start = &fn[i];
    }
    srcname = fnames[0];
    if (!start && !objmode) die(1, "no '_start' function defined");
    if (start && start->np != 0) die(start->line, "'_start' must take no parameters");

    layout_data();
    for (int i = 0; i < nfn; i++) gen_func(&fn[i]);
    /* dead-code removal: every function was type-checked above; now drop the ones nothing reaches
       (roots: _start for an executable; every 'global' function for an object file) */
    int removed = 0;
    {
        int total_code = clen;
        for (int i = 0; i < nfn; i++) endp[i] = i + 1 < nfn ? fn[i + 1].addr : total_code;
        for (int i = 0; i < nfn; i++) { fsize[i] = endp[i] - fn[i].addr; live[i] = keep_unused || (objmode && fn[i].global); }
        if (start) live[start - fn] = 1;
        for (int changed = 1; changed; ) {
            changed = 0;
            for (int k = 0; k < ncfix; k++) {
                int caller = -1;
                for (int i = 0; i < nfn; i++) if (fsize[i] && cfix[k].pos >= fn[i].addr && cfix[k].pos < endp[i]) { caller = i; break; }
                if (caller >= 0 && live[caller] && !live[callee_of[k]]) { live[callee_of[k]] = 1; changed = 1; }
            }
        }
        if (!keep_unused) {
            uint8_t *nc = malloc(total_code ? total_code : 1); int nl = 0;
            static int newaddr[1024];
            for (int i = 0; i < nfn; i++) {
                if (!live[i]) { if (!fn[i].ext) removed++; newaddr[i] = -1; continue; }
                newaddr[i] = nl; memcpy(nc + nl, code + fn[i].addr, fsize[i]); nl += fsize[i];
            }
            for (int k = 0; k < ncfix; k++)
                for (int i = 0; i < nfn; i++) if (fsize[i] && cfix[k].pos >= fn[i].addr && cfix[k].pos < endp[i]) { cfix[k].pos = newaddr[i] < 0 ? -1 : newaddr[i] + (cfix[k].pos - fn[i].addr); break; }
            for (int k = 0; k < ndrel; k++)
                for (int i = 0; i < nfn; i++) if (fsize[i] && drel[k].pos >= fn[i].addr && drel[k].pos < endp[i]) { drel[k].pos = newaddr[i] < 0 ? -1 : newaddr[i] + (drel[k].pos - fn[i].addr); break; }
            free(code); code = nc; ccap = cl_ = nl;
            for (int i = 0; i < nfn; i++) fn[i].addr = newaddr[i];
        }
    }
    for (int i = 0; i < ncfix; i++) if (cfix[i].pos >= 0 && fn[callee_of[i]].addr >= 0 && !fn[callee_of[i]].ext) patch_to(cfix[i].pos, fn[callee_of[i]].addr);

    if (objmode) { write_object(out); 
        fprintf(stderr, "silicon: %d file%s -> %s (object: %d bytes of code, %d bytes of data, %d bytes read-only)\n", nin, nin == 1 ? "" : "s", out, clen, dlen, rlen);
        return 0; }

    /* up to three segments: headers+code (R+X), .rodata (R), .data (R+W); nothing is both writable and executable */
    int has_ro = rlen > 0, has_data = dlen > 0, nph = 1 + has_ro + has_data;
    const uint32_t text_off = 64 + nph * 56;
    if (clen > (int)(RO_BASE - BASE - text_off)) die(0, "program code is larger than 2 MiB");
    if (rlen > (int)(DATA_BASE - RO_BASE)) die(0, "read-only data is larger than 2 MiB");
    u64 seg1 = text_off + clen;
    u64 ro_off = (seg1 + 0xFFF) & ~(u64)0xFFF;
    u64 data_off = has_ro ? ((ro_off + rlen + 0xFFF) & ~(u64)0xFFF) : ro_off;
    uint8_t eh[64 + 3 * 56]; memset(eh, 0, sizeof eh);
    const uint8_t magic[8] = { 0x7f, 'E', 'L', 'F', 2, 1, 1, 0 };
    memcpy(eh, magic, 8);
    uint16_t v16; uint32_t v32; u64 v64;
    v16 = 2;    memcpy(eh + 16, &v16, 2);          /* ET_EXEC */
    v16 = 0x3E; memcpy(eh + 18, &v16, 2);          /* x86-64 */
    v32 = 1;    memcpy(eh + 20, &v32, 4);
    v64 = BASE + text_off + start->addr; memcpy(eh + 24, &v64, 8);   /* entry */
    v64 = 64;   memcpy(eh + 32, &v64, 8);          /* phoff */
    v16 = 64;   memcpy(eh + 52, &v16, 2);          /* ehsize */
    v16 = 56;   memcpy(eh + 54, &v16, 2);          /* phentsize */
    v16 = nph;  memcpy(eh + 56, &v16, 2);          /* phnum */
    v16 = 64;   memcpy(eh + 58, &v16, 2);          /* shentsize */
    #define PHDR(n, flags, off, vaddr, sz) do { uint8_t *ph = eh + 64 + (n) * 56; u64 _o = (off), _v = (vaddr), _s = (sz), _a = 0x1000; \
        uint32_t _t = 1, _f = (flags); memcpy(ph, &_t, 4); memcpy(ph + 4, &_f, 4); memcpy(ph + 8, &_o, 8); memcpy(ph + 16, &_v, 8); \
        memcpy(ph + 24, &_v, 8); memcpy(ph + 32, &_s, 8); memcpy(ph + 40, &_s, 8); memcpy(ph + 48, &_a, 8); } while (0)
    int pn = 0;
    PHDR(pn++, 5, 0, BASE, seg1);                                  /* code, R+X */
    if (has_ro)   PHDR(pn++, 4, ro_off, RO_BASE, rlen);            /* .rodata, R */
    if (has_data) PHDR(pn++, 6, data_off, DATA_BASE, dlen);        /* .data, R+W */

    FILE *fp = fopen(out, "wb");
    if (!fp) { fprintf(stderr, "silicon: cannot write '%s'\n", out); return 1; }
    fwrite(eh, 1, text_off, fp);
    fwrite(code, 1, clen, fp);
    static uint8_t zeros[4096];
    #define PAD_TO(from, to) for (u64 at = (from); at < (to); ) { u64 n = (to) - at; if (n > sizeof zeros) n = sizeof zeros; fwrite(zeros, 1, n, fp); at += n; }
    if (has_ro)   { PAD_TO(seg1, ro_off); fwrite(rdat, 1, rlen, fp); }
    if (has_data) { PAD_TO(has_ro ? ro_off + rlen : seg1, data_off); fwrite(dat, 1, dlen, fp); }
    fclose(fp);
    chmod(out, 0755);
    if (nin == 1) fprintf(stderr, "silicon: %s -> %s (%d bytes of code, %d bytes of data%s)\n", in, out, clen, dlen + rlen, rlen ? " incl. read-only" : "");
    else fprintf(stderr, "silicon: %d files -> %s (%d bytes of code, %d bytes of data%s%s)\n", nin, out, clen, dlen + rlen, rlen ? " incl. read-only" : "", removed ? ", unused functions removed" : "");
    return 0;
}
