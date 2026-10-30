/*
 * C-for-U: a small C-to-KR16-assembly compiler.
 *
 * Input:  a source file, read in refillable chunks into a bounded buffer.
 * Output: <input-base>.s, or the path after -o, in the VM sandbox.
 * Startup: prints each command-line argument as an assembler comment.
 *
 * Supported subset: int/char, locals, globals, one-dimensional arrays,
 * arithmetic/comparison/bitwise/logical expressions, if/else, while, for,
 * do/while, break, continue, prefix/postfix ++/--, function definitions and
 * calls (up to three arguments), strings, printf calls, casts, and sizeof.
 * Input is limited to 4095 bytes and 384 tokens in this pass.
 * Not supported: structs/unions, switch, ternary, typedef, pointer declarators,
 * and calls with more than three arguments.
 */

#include <stdio.h>

char source[4096];
FILE *outfile;
void *malloc(int n);
void free(void *p);
void exit(int n);
char *strcpy(char *d, char *s);

#define T_EOF 1
#define T_NUM 2
#define T_ID 3
#define T_STR 4
#define T_INT 10
#define T_CHAR 11
#define T_VOID 12
#define T_IF 13
#define T_ELSE 14
#define T_WHILE 15
#define T_FOR 16
#define T_RETURN 17
#define T_BREAK 18
#define T_CONTINUE 19
#define T_DO 20
#define T_SIZEOF 21

#define T_EQ 300
#define T_NE 301
#define T_LE 302
#define T_GE 303
#define T_ANDAND 304
#define T_OROR 305
#define T_SHL 306
#define T_SHR 307
#define T_INC 308
#define T_DEC 309
#define T_ADDASSIGN 310
#define T_SUBASSIGN 311

#define MAX_TOKENS 384
#define READ_CHUNK 128

int *tkind;
int *tval;
char *ttext;
int *tlen;
int tcount;
int tpos;

char names[1536];
int noff[64];
int ntype[64];
int nelem[64];
int nsize[64];
int ncount;
int frame_off;

char gnames[384];
int gtype[16];
int gelem[16];
int gsize[16];
int gcount;

char funcs[384];
int fcount;
char calls[768];
int ccount;

char strtab[640];
int scount;

char current[24];
int label_n;
int has_main;
int break_stack[16];
int continue_stack[16];
int loop_depth;
int base_local;

void outch(int c);
void outs(char *s);
void outn(int n);
void err(char *m);
int same(char *a, char *b);
void copy_name(char *d, char *s, int max);
char *tok_text(int i);
void addtok(int kind, int value, char *text, int n);
void lex_source(void);
void add_local(char *name, int type, int elem, int size);
int find_local(char *name);
int find_global(char *name);
void add_global(char *name, int type, int elem, int size);
void add_func(char *name);
void add_call(char *name);
int add_string(char *s, int n);
void emit_label(int id);
void emit_ret_label(void);
void emit_branch(char *op, int id);
int new_label(void);
void gen_name_addr(char *name);
void gen_incdec(char *name, int delta);
int gen_name_load(char *name);
int parse_type(void);
void parse_program(void);
void parse_function(int type, char *name);
void parse_global(int type, char *name);
void parse_compound(void);
void parse_statement(void);
void parse_decl(void);
void parse_expr_or_assign(void);
void parse_assign(void);
int parse_lvalue(void);
int parse_expr(void);
int parse_lor(void);
int parse_land(void);
int parse_bor(void);
int parse_bxor(void);
int parse_band(void);
int parse_eq(void);
int parse_rel(void);
int parse_shift(void);
int parse_add(void);
int parse_mul(void);
int parse_unary(void);
int parse_postfix(void);
int parse_primary(void);
void emit_cmp_result(char *branch);
void emit_externs(void);
void emit_strings(void);
void read_source(char *path);

int same(char *a, char *b) {
    while (*a != 0 && *b != 0) {
        if (*a != *b) return 0;
        a = a + 1;
        b = b + 1;
    }
    return *a == *b;
}

void copy_name(char *d, char *s, int max) {
    int i;
    i = 0;
    while (i < max - 1 && s[i] != 0) {
        d[i] = s[i];
        i = i + 1;
    }
    d[i] = 0;
}

void outch(int c) {
    if (outfile != 0) {
        fputc(c, outfile);
    } else {
        putchar(c);
    }
}

void outs(char *s) {
    while (*s != 0) {
        outch(*s);
        s = s + 1;
    }
}

void outn(int n) {
    char b[8];
    int i;
    if (n < 0) {
        outch(45);
        n = 0 - n;
    }
    if (n == 0) {
        outch(48);
        return;
    }
    i = 0;
    while (n > 0) {
        b[i] = n % 10 + 48;
        n = n / 10;
        i = i + 1;
    }
    while (i > 0) {
        i = i - 1;
        outch(b[i]);
    }
}

void err(char *m) {
    if (outfile != 0) {
        fclose(outfile);
        outfile = 0;
    }
    outs("; C-for-U error: ");
    outs(m);
    outs("\n");
    exit(1);
}

char *tok_text(int i) {
    return ttext + i * 32;
}

void addtok(int kind, int value, char *text, int n) {
    char *d;
    int i;
    if (tcount >= MAX_TOKENS) err("too many tokens");
    tkind[tcount] = kind;
    tval[tcount] = value;
    tlen[tcount] = n;
    d = ttext + tcount * 32;
    i = 0;
    while (i < n && i < 31) {
        d[i] = text[i];
        i = i + 1;
    }
    d[i] = 0;
    tcount = tcount + 1;
}

int is_alpha(int c) {
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c == '_') return 1;
    return 0;
}

int is_digit(int c) {
    if (c >= '0' && c <= '9') return 1;
    return 0;
}

int is_alnum(int c) {
    if (is_alpha(c)) return 1;
    if (is_digit(c)) return 1;
    return 0;
}

int keyword(char *s) {
    if (same(s, "int")) return T_INT;
    if (same(s, "char")) return T_CHAR;
    if (same(s, "void")) return T_VOID;
    if (same(s, "if")) return T_IF;
    if (same(s, "else")) return T_ELSE;
    if (same(s, "while")) return T_WHILE;
    if (same(s, "for")) return T_FOR;
    if (same(s, "return")) return T_RETURN;
    if (same(s, "break")) return T_BREAK;
    if (same(s, "continue")) return T_CONTINUE;
    if (same(s, "do")) return T_DO;
    if (same(s, "sizeof")) return T_SIZEOF;
    return T_ID;
}

int two(char *p, int a, int b) {
    if (p[0] == a && p[1] == b) return 1;
    return 0;
}

void lex_source(void) {
    char *p;
    char *start;
    char word[32];
    int n;
    p = source;
    while (*p != 0) {
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p = p + 1;
        } else if (p[0] == '/' && p[1] == '/') {
            while (*p != 0 && *p != '\n') p = p + 1;
        } else if (p[0] == '/' && p[1] == '*') {
            p = p + 2;
            while (*p != 0 && !(p[0] == '*' && p[1] == '/')) p = p + 1;
            if (*p != 0) p = p + 2;
        } else if (*p == '"') {
            p = p + 1;
            start = p;
            n = 0;
            while (*p != 0 && *p != '"') {
                if (*p == '\\' && p[1] != 0) {
                    p = p + 2;
                    n = n + 2;
                } else {
                    p = p + 1;
                    n = n + 1;
                }
            }
            addtok(T_STR, 0, start, n);
            if (*p == '"') p = p + 1;
        } else if (is_alpha(*p)) {
            start = p;
            while (is_alnum(*p)) p = p + 1;
            n = p - start;
            copy_name(word, start, n + 1);
            addtok(keyword(word), 0, start, n);
        } else if (is_digit(*p)) {
            int value;
            value = 0;
            while (is_digit(*p)) {
                value = value * 10 + (*p - '0');
                p = p + 1;
            }
            addtok(T_NUM, value, p, 0);
        } else if (two(p, '=', '=')) {
            addtok(T_EQ, 0, p, 2); p = p + 2;
        } else if (two(p, '!', '=')) {
            addtok(T_NE, 0, p, 2); p = p + 2;
        } else if (two(p, '<', '=')) {
            addtok(T_LE, 0, p, 2); p = p + 2;
        } else if (two(p, '>', '=')) {
            addtok(T_GE, 0, p, 2); p = p + 2;
        } else if (two(p, '&', '&')) {
            addtok(T_ANDAND, 0, p, 2); p = p + 2;
        } else if (two(p, '|', '|')) {
            addtok(T_OROR, 0, p, 2); p = p + 2;
        } else if (two(p, '<', '<')) {
            addtok(T_SHL, 0, p, 2); p = p + 2;
        } else if (two(p, '>', '>')) {
            addtok(T_SHR, 0, p, 2); p = p + 2;
        } else if (two(p, '+', '+')) {
            addtok(T_INC, 0, p, 2); p = p + 2;
        } else if (two(p, '-', '-')) {
            addtok(T_DEC, 0, p, 2); p = p + 2;
        } else if (two(p, '+', '=')) {
            addtok(T_ADDASSIGN, 0, p, 2); p = p + 2;
        } else if (two(p, '-', '=')) {
            addtok(T_SUBASSIGN, 0, p, 2); p = p + 2;
        } else {
            addtok(*p, 0, p, 1);
            p = p + 1;
        }
    }
    addtok(T_EOF, 0, p, 0);
}

int find_local(char *name) {
    int i;
    i = 0;
    while (i < ncount) {
        if (same(names + i * 24, name)) return i;
        i = i + 1;
    }
    return 0 - 1;
}

int find_global(char *name) {
    int i;
    i = 0;
    while (i < gcount) {
        if (same(gnames + i * 24, name)) return i;
        i = i + 1;
    }
    return 0 - 1;
}

void add_local(char *name, int type, int elem, int size) {
    if (ncount >= 64) err("too many locals");
    if (frame_off % 2 != 0) frame_off = frame_off + 1;
    copy_name(names + ncount * 24, name, 24);
    noff[ncount] = frame_off;
    ntype[ncount] = type;
    nelem[ncount] = elem;
    nsize[ncount] = size;
    frame_off = frame_off + size;
    ncount = ncount + 1;
}

void add_global(char *name, int type, int elem, int size) {
    if (gcount >= 16) err("too many globals");
    copy_name(gnames + gcount * 24, name, 24);
    gtype[gcount] = type;
    gelem[gcount] = elem;
    gsize[gcount] = size;
    gcount = gcount + 1;
}

void add_func(char *name) {
    int i;
    i = 0;
    while (i < fcount) {
        if (same(funcs + i * 24, name)) return;
        i = i + 1;
    }
    if (fcount >= 16) err("too many functions");
    copy_name(funcs + fcount * 24, name, 24);
    fcount = fcount + 1;
}

void add_call(char *name) {
    int i;
    i = 0;
    while (i < ccount) {
        if (same(calls + i * 24, name)) return;
        i = i + 1;
    }
    if (ccount >= 32) err("too many calls");
    copy_name(calls + ccount * 24, name, 24);
    ccount = ccount + 1;
}

int add_string(char *s, int n) {
    int i;
    if (scount >= 8) err("too many strings");
    copy_name(strtab + scount * 80, s, 80);
    i = scount;
    scount = scount + 1;
    return i;
}

int new_label(void) {
    label_n = label_n + 1;
    return label_n;
}

void emit_label(int id) {
    outs(".Lcfu_");
    outn(id);
    outs(":\n");
}

void emit_branch(char *op, int id) {
    outs("  ");
    outs(op);
    outs(" .Lcfu_");
    outn(id);
    outs("\n");
}

void emit_ret_label(void) {
    outs(".Lret_");
    outs(current);
    outs(":\n");
}

void gen_name_addr(char *name) {
    int i;
    i = find_local(name);
    if (i >= 0) {
        base_local = 1;
        outs("  LEA R0, [R4-");
        outn(noff[i]);
        outs("]\n");
        return;
    }
    i = find_global(name);
    if (i >= 0) {
        base_local = 0;
        outs("  MOV R0, #");
        outs(name);
        outs("\n");
        return;
    }
    err("unknown variable");
}

void gen_incdec(char *name, int delta) {
    int i;
    int kind;
    i = find_local(name);
    if (i >= 0) {
        kind = ntype[i];
    } else {
        i = find_global(name);
        if (i < 0) err("unknown variable");
        kind = gtype[i];
    }
    gen_name_addr(name);
    outs("  PUSH R0\n");
    if (kind == 0) outs("  LDB R0, [R0]\n");
    else outs("  LD R0, [R0]\n");
    outs("  MOV R1, R0\n");
    if (delta > 0) outs("  ADD R0, #1\n");
    else outs("  SUB R0, #1\n");
    outs("  POP R2\n");
    if (kind == 0) outs("  STB [R2], R0\n");
    else outs("  ST [R2], R0\n");
    outs("  MOV R0, R1\n");
}

int gen_name_load(char *name) {
    int i;
    i = find_local(name);
    if (i >= 0) {
        if (ntype[i] == 2 || ntype[i] == 3) {
            gen_name_addr(name);
            return ntype[i];
        }
        if (ntype[i] == 0) {
            outs("  LDB R0, [R4-");
            outn(noff[i]);
            outs("]\n");
            return 0;
        }
        outs("  LD R0, [R4-");
        outn(noff[i]);
        outs("]\n");
        return 1;
    }
    i = find_global(name);
    if (i >= 0) {
        if (gtype[i] == 2 || gtype[i] == 3) {
            outs("  MOV R0, #");
            outs(name);
            outs("\n");
            return gtype[i];
        }
        if (gtype[i] == 0) {
            outs("  LDB R0, [");
            outs(name);
            outs("]\n");
            return 0;
        }
        outs("  LD R0, [");
        outs(name);
        outs("]\n");
        return 1;
    }
    err("unknown variable");
    return 1;
}

int parse_type(void) {
    if (tkind[tpos] == T_INT) {
        tpos = tpos + 1;
        return 1;
    }
    if (tkind[tpos] == T_CHAR) {
        tpos = tpos + 1;
        return 0;
    }
    if (tkind[tpos] == T_VOID) {
        tpos = tpos + 1;
        return 2;
    }
    err("expected type");
    return 1;
}

void emit_cmp_result(char *branch) {
    int yes;
    int end;
    yes = new_label();
    end = new_label();
    outs("  ");
    outs(branch);
    outs(" .Lcfu_");
    outn(yes);
    outs("\n");
    outs("  MOV R0, #0\n  BRA .Lcfu_");
    outn(end);
    outs("\n");
    emit_label(yes);
    outs("  MOV R0, #1\n");
    emit_label(end);
}

int parse_primary(void) {
    int k;
    int value;
    int i;
    char *name;
    if (tkind[tpos] == T_NUM) {
        value = tval[tpos];
        tpos = tpos + 1;
        outs("  MOV R0, #");
        outn(value);
        outs("\n");
        return 1;
    }
    if (tkind[tpos] == T_STR) {
        value = add_string(tok_text(tpos), tlen[tpos]);
        tpos = tpos + 1;
        outs("  MOV R0, #.Lstr");
        outn(value);
        outs("\n");
        return 1;
    }
    if (tkind[tpos] == '(') {
        tpos = tpos + 1;
        k = parse_expr();
        if (tkind[tpos] != ')') err("expected )");
        tpos = tpos + 1;
        return k;
    }
    if (tkind[tpos] == T_ID) {
        name = tok_text(tpos);
        tpos = tpos + 1;
        if (tkind[tpos] == '(') {
            int count;
            int r;
            tpos = tpos + 1;
            count = 0;
            if (tkind[tpos] != ')') {
                while (1) {
                    parse_expr();
                    outs("  PUSH R0\n");
                    count = count + 1;
                    if (tkind[tpos] == ',') {
                        tpos = tpos + 1;
                    } else {
                        break;
                    }
                }
            }
            if (tkind[tpos] != ')') err("expected )");
            tpos = tpos + 1;
            if (count > 3) err("more than three arguments");
            r = count;
            while (r > 0) {
                outs("  POP R");
                outn(r);
                outs("\n");
                r = r - 1;
            }
            add_call(name);
            outs("  CALL ");
            outs(name);
            outs("\n");
            return 1;
        }
        if (tkind[tpos] == T_INC || tkind[tpos] == T_DEC) {
            int delta;
            delta = tkind[tpos] == T_INC ? 1 : 0 - 1;
            tpos = tpos + 1;
            i = find_local(name);
            if (i >= 0) {
                gen_incdec(name, delta);
                return ntype[i];
            }
            i = find_global(name);
            if (i < 0) err("unknown variable");
            gen_incdec(name, delta);
            return gtype[i];
        }
        return gen_name_load(name);
    }
    (void)k;
    (void)i;
    err("expected expression");
    return 1;
}

int parse_postfix(void) {
    int type;
    int elem;
    elem = 1;
    type = parse_primary();
    while (tkind[tpos] == '[') {
        tpos = tpos + 1;
        outs("  PUSH R0\n");
        parse_expr();
        if (type == 3) outs("  SHL R0, #1\n");
        outs("  MOV R1, R0\n  POP R0\n");
        if (base_local) outs("  SUB R0, R1\n");
        else outs("  ADD R0, R1\n");
        if (tkind[tpos] != ']') err("expected ]");
        tpos = tpos + 1;
        if (type == 3) {
            outs("  LD R0, [R0]\n");
            elem = 1;
        } else {
            outs("  LDB R0, [R0]\n");
            elem = 0;
        }
        type = elem;
    }
    return type;
}

int parse_unary(void) {
    int type;
    int i;
    char *name;
    if (tkind[tpos] == T_INC || tkind[tpos] == T_DEC) {
        int delta;
        delta = tkind[tpos] == T_INC ? 1 : 0 - 1;
        tpos = tpos + 1;
        if (tkind[tpos] != T_ID) err("expected variable");
        name = tok_text(tpos);
        tpos = tpos + 1;
        i = find_local(name);
        if (i >= 0) {
            gen_incdec(name, delta);
            return ntype[i];
        }
        i = find_global(name);
        if (i < 0) err("unknown variable");
        gen_incdec(name, delta);
        return gtype[i];
    }
    if (tkind[tpos] == T_SIZEOF) {
        tpos = tpos + 1;
        if (tkind[tpos] == '(' &&
            (tkind[tpos + 1] == T_INT || tkind[tpos + 1] == T_CHAR)) {
            tpos = tpos + 1;
            type = parse_type();
            if (tkind[tpos] != ')') err("expected )");
            tpos = tpos + 1;
            outs("  MOV R0, #");
            if (type == 0) outn(1);
            else outn(2);
            outs("\n");
            return 1;
        }
        parse_unary();
        outs("  MOV R0, #2\n");
        return 1;
    }
    if (tkind[tpos] == '(' &&
        (tkind[tpos + 1] == T_INT || tkind[tpos + 1] == T_CHAR)) {
        tpos = tpos + 1;
        type = parse_type();
        if (tkind[tpos] != ')') err("expected )");
        tpos = tpos + 1;
        parse_unary();
        if (type == 0) outs("  AND R0, #0xFF\n");
        return type;
    }
    if (tkind[tpos] == '-') {
        tpos = tpos + 1;
        type = parse_unary();
        outs("  NEG R0\n");
        return type;
    }
    if (tkind[tpos] == '~') {
        tpos = tpos + 1;
        type = parse_unary();
        outs("  NOT R0\n");
        return type;
    }
    if (tkind[tpos] == '!') {
        tpos = tpos + 1;
        parse_unary();
        outs("  CMP R0, #0\n");
        emit_cmp_result("BEQ");
        return 1;
    }
    if (tkind[tpos] == '&') {
        tpos = tpos + 1;
        parse_lvalue();
        return 1;
    }
    if (tkind[tpos] == '*') {
        tpos = tpos + 1;
        parse_unary();
        outs("  LD R0, [R0]\n");
        return 1;
    }
    return parse_postfix();
}

int parse_mul(void) {
    int type;
    int op;
    type = parse_unary();
    while (tkind[tpos] == '*' || tkind[tpos] == '/' ||
           tkind[tpos] == '%') {
        op = tkind[tpos];
        tpos = tpos + 1;
        outs("  PUSH R0\n");
        parse_unary();
        outs("  MOV R1, R0\n  POP R0\n");
        if (op == '*') outs("  MUL R0, R1\n");
        if (op == '/') outs("  DIV R0, R1\n");
        if (op == '%') outs("  MOD R0, R1\n");
        type = 1;
    }
    return type;
}

int parse_add(void) {
    int type;
    int op;
    type = parse_mul();
    while (tkind[tpos] == '+' || tkind[tpos] == '-') {
        op = tkind[tpos];
        tpos = tpos + 1;
        outs("  PUSH R0\n");
        parse_mul();
        outs("  MOV R1, R0\n  POP R0\n");
        if (op == '+') outs("  ADD R0, R1\n");
        if (op == '-') outs("  SUB R0, R1\n");
        type = 1;
    }
    return type;
}

int parse_shift(void) {
    int type;
    int op;
    type = parse_add();
    while (tkind[tpos] == T_SHL || tkind[tpos] == T_SHR) {
        op = tkind[tpos];
        tpos = tpos + 1;
        outs("  PUSH R0\n");
        parse_add();
        outs("  MOV R1, R0\n  POP R0\n");
        if (op == T_SHL) outs("  SHL R0, R1\n");
        if (op == T_SHR) outs("  SAR R0, R1\n");
        type = 1;
    }
    return type;
}

int parse_rel(void) {
    int type;
    int op;
    type = parse_shift();
    while (tkind[tpos] == '<' || tkind[tpos] == '>' ||
           tkind[tpos] == T_LE || tkind[tpos] == T_GE) {
        op = tkind[tpos];
        tpos = tpos + 1;
        outs("  PUSH R0\n");
        parse_shift();
        outs("  MOV R1, R0\n  POP R0\n  CMP R0, R1\n");
        if (op == '<') emit_cmp_result("BLT");
        if (op == '>') emit_cmp_result("BGT");
        if (op == T_LE) emit_cmp_result("BLE");
        if (op == T_GE) emit_cmp_result("BGE");
        type = 1;
    }
    return type;
}

int parse_eq(void) {
    int type;
    int op;
    type = parse_rel();
    while (tkind[tpos] == T_EQ || tkind[tpos] == T_NE) {
        op = tkind[tpos];
        tpos = tpos + 1;
        outs("  PUSH R0\n");
        parse_rel();
        outs("  MOV R1, R0\n  POP R0\n  CMP R0, R1\n");
        if (op == T_EQ) emit_cmp_result("BEQ");
        if (op == T_NE) emit_cmp_result("BNE");
        type = 1;
    }
    return type;
}

int parse_band(void) {
    int type;
    type = parse_eq();
    while (tkind[tpos] == '&') {
        tpos = tpos + 1;
        outs("  PUSH R0\n");
        parse_eq();
        outs("  MOV R1, R0\n  POP R0\n  AND R0, R1\n");
        type = 1;
    }
    return type;
}

int parse_bxor(void) {
    int type;
    type = parse_band();
    while (tkind[tpos] == '^') {
        tpos = tpos + 1;
        outs("  PUSH R0\n");
        parse_band();
        outs("  MOV R1, R0\n  POP R0\n  XOR R0, R1\n");
        type = 1;
    }
    return type;
}

int parse_bor(void) {
    int type;
    type = parse_bxor();
    while (tkind[tpos] == '|') {
        tpos = tpos + 1;
        outs("  PUSH R0\n");
        parse_bxor();
        outs("  MOV R1, R0\n  POP R0\n  OR R0, R1\n");
        type = 1;
    }
    return type;
}

int parse_land(void) {
    int type;
    int no;
    int end;
    type = parse_bor();
    while (tkind[tpos] == T_ANDAND) {
        tpos = tpos + 1;
        no = new_label();
        end = new_label();
        outs("  CMP R0, #0\n");
        emit_branch("BEQ", no);
        parse_bor();
        outs("  CMP R0, #0\n");
        emit_branch("BEQ", no);
        outs("  MOV R0, #1\n");
        emit_branch("BRA", end);
        emit_label(no);
        outs("  MOV R0, #0\n");
        emit_label(end);
        type = 1;
    }
    return type;
}

int parse_lor(void) {
    int type;
    int yes;
    int end;
    type = parse_land();
    while (tkind[tpos] == T_OROR) {
        tpos = tpos + 1;
        yes = new_label();
        end = new_label();
        outs("  CMP R0, #0\n");
        emit_branch("BNE", yes);
        parse_land();
        outs("  CMP R0, #0\n");
        emit_branch("BNE", yes);
        outs("  MOV R0, #0\n");
        emit_branch("BRA", end);
        emit_label(yes);
        outs("  MOV R0, #1\n");
        emit_label(end);
        type = 1;
    }
    return type;
}

int parse_expr(void) {
    return parse_lor();
}

int parse_lvalue(void) {
    int type;
    int i;
    char *name;
    if (tkind[tpos] == '*') {
        tpos = tpos + 1;
        parse_expr();
        return 1;
    }
    if (tkind[tpos] != T_ID) err("expected lvalue");
    name = tok_text(tpos);
    tpos = tpos + 1;
    i = find_local(name);
    if (i < 0) i = find_global(name);
    if (i < 0) err("unknown lvalue");
    gen_name_addr(name);
    type = 1;
    if (find_local(name) >= 0) {
        i = find_local(name);
        type = ntype[i];
    } else {
        i = find_global(name);
        type = gtype[i];
    }
    if (type == 2 || type == 3) {
        if (tkind[tpos] != '[') err("array needs index");
        tpos = tpos + 1;
        outs("  PUSH R0\n");
        parse_expr();
        if (type == 3) outs("  SHL R0, #1\n");
        outs("  MOV R1, R0\n  POP R0\n");
        if (base_local) outs("  SUB R0, R1\n");
        else outs("  ADD R0, R1\n");
        if (tkind[tpos] != ']') err("expected ]");
        tpos = tpos + 1;
        if (type == 3) return 1;
        return 0;
    }
    return type;
}

void parse_assign(void) {
    int type;
    type = parse_lvalue();
    if (tkind[tpos] != '=') err("expected =");
    tpos = tpos + 1;
    outs("  PUSH R0\n");
    parse_expr();
    outs("  MOV R1, R0\n  POP R0\n");
    if (type == 0) outs("  STB [R0], R1\n");
    else outs("  ST [R0], R1\n");
    outs("  MOV R0, R1\n");
}

int has_assign(void) {
    int i;
    int depth;
    i = tpos;
    depth = 0;
    while (tkind[i] != T_EOF) {
        if (tkind[i] == '(' || tkind[i] == '[') depth = depth + 1;
        if (tkind[i] == ')' || tkind[i] == ']') {
            if (depth == 0) return 0;
            depth = depth - 1;
        }
        if (depth == 0 && tkind[i] == ';') return 0;
        if (depth == 0 && tkind[i] == '=') return 1;
        i = i + 1;
    }
    return 0;
}

void parse_expr_or_assign(void) {
    if (has_assign()) parse_assign();
    else parse_expr();
}

void parse_decl(void) {
    int type;
    int elem;
    int count;
    int size;
    char *name;
    type = parse_type();
    while (1) {
        if (tkind[tpos] != T_ID) err("expected name");
        name = tok_text(tpos);
        tpos = tpos + 1;
        if (tkind[tpos] == '[') {
            tpos = tpos + 1;
            if (tkind[tpos] != T_NUM) err("expected array size");
            count = tval[tpos];
            tpos = tpos + 1;
            if (tkind[tpos] != ']') err("expected ]");
            tpos = tpos + 1;
            elem = type;
            if (elem == 1) size = count * 2;
            else size = count;
            add_local(name, elem + 2, elem, size);
        } else {
            elem = type;
            if (elem == 1) size = 2;
            else size = 1;
            add_local(name, elem, elem, size);
            if (tkind[tpos] == '=') {
                tpos = tpos + 1;
                parse_expr();
                if (elem == 0) outs("  STB [R4-");
                else outs("  ST [R4-");
                outn(noff[ncount - 1]);
                outs("], R0\n");
            }
        }
        if (tkind[tpos] == ',') {
            tpos = tpos + 1;
        } else {
            break;
        }
    }
    if (tkind[tpos] != ';') err("expected ;");
    tpos = tpos + 1;
}

void parse_compound(void) {
    if (tkind[tpos] != '{') err("expected {");
    tpos = tpos + 1;
    while (tkind[tpos] != '}' && tkind[tpos] != T_EOF) {
        parse_statement();
    }
    if (tkind[tpos] != '}') err("expected }");
    tpos = tpos + 1;
}

void parse_statement(void) {
    int a;
    int b;
    int c;
    int d;
    if (tkind[tpos] == ';') {
        tpos = tpos + 1;
        return;
    }
    if (tkind[tpos] == '{') {
        parse_compound();
        return;
    }
    if (tkind[tpos] == T_INT || tkind[tpos] == T_CHAR) {
        parse_decl();
        return;
    }
    if (tkind[tpos] == T_IF) {
        tpos = tpos + 1;
        if (tkind[tpos] != '(') err("expected (");
        tpos = tpos + 1;
        parse_expr();
        if (tkind[tpos] != ')') err("expected )");
        tpos = tpos + 1;
        a = new_label();
        b = new_label();
        outs("  CMP R0, #0\n");
        emit_branch("BEQ", a);
        parse_statement();
        emit_branch("BRA", b);
        emit_label(a);
        if (tkind[tpos] == T_ELSE) {
            tpos = tpos + 1;
            parse_statement();
        }
        emit_label(b);
        return;
    }
    if (tkind[tpos] == T_DO) {
        tpos = tpos + 1;
        a = new_label();
        b = new_label();
        emit_label(a);
        break_stack[loop_depth] = b;
        continue_stack[loop_depth] = a;
        loop_depth = loop_depth + 1;
        parse_statement();
        loop_depth = loop_depth - 1;
        if (tkind[tpos] != T_WHILE) err("expected while");
        tpos = tpos + 1;
        if (tkind[tpos] != '(') err("expected (");
        tpos = tpos + 1;
        parse_expr();
        if (tkind[tpos] != ')') err("expected )");
        tpos = tpos + 1;
        if (tkind[tpos] != ';') err("expected ;");
        tpos = tpos + 1;
        outs("  CMP R0, #0\n");
        emit_branch("BNE", a);
        emit_label(b);
        return;
    }
    if (tkind[tpos] == T_WHILE) {
        tpos = tpos + 1;
        a = new_label();
        b = new_label();
        emit_label(a);
        if (tkind[tpos] != '(') err("expected (");
        tpos = tpos + 1;
        parse_expr();
        if (tkind[tpos] != ')') err("expected )");
        tpos = tpos + 1;
        outs("  CMP R0, #0\n");
        emit_branch("BEQ", b);
        break_stack[loop_depth] = b;
        continue_stack[loop_depth] = a;
        loop_depth = loop_depth + 1;
        parse_statement();
        loop_depth = loop_depth - 1;
        emit_branch("BRA", a);
        emit_label(b);
        return;
    }
    if (tkind[tpos] == T_FOR) {
        int start;
        int end;
        int next;
        int inc_start;
        int inc_end;
        int after_body;
        int depth;
        tpos = tpos + 1;
        if (tkind[tpos] != '(') err("expected (");
        tpos = tpos + 1;
        if (tkind[tpos] == T_INT || tkind[tpos] == T_CHAR) {
            parse_decl();
        } else {
            if (tkind[tpos] != ';') {
                if (has_assign()) parse_assign();
                else parse_expr();
            }
            if (tkind[tpos] != ';') err("expected ;");
            tpos = tpos + 1;
        }
        start = new_label();
        end = new_label();
        next = new_label();
        emit_label(start);
        if (tkind[tpos] != ';') {
            parse_expr();
            outs("  CMP R0, #0\n");
            emit_branch("BEQ", end);
        }
        if (tkind[tpos] != ';') err("expected ;");
        tpos = tpos + 1;
        inc_start = tpos;
        depth = 0;
        while (tkind[tpos] != T_EOF) {
            if (tkind[tpos] == '(' || tkind[tpos] == '[') depth = depth + 1;
            if (tkind[tpos] == ')' || tkind[tpos] == ']') {
                if (depth == 0) break;
                depth = depth - 1;
            }
            tpos = tpos + 1;
        }
        inc_end = tpos;
        if (tkind[tpos] != ')') err("expected )");
        tpos = tpos + 1;
        break_stack[loop_depth] = end;
        continue_stack[loop_depth] = next;
        loop_depth = loop_depth + 1;
        parse_statement();
        loop_depth = loop_depth - 1;
        emit_label(next);
        after_body = tpos;
        tpos = inc_start;
        if (tkind[tpos] != ')') {
            if (has_assign()) parse_assign();
            else parse_expr();
        }
        tpos = after_body;
        emit_branch("BRA", start);
        emit_label(end);
        (void)inc_end;
        return;
    }
    if (tkind[tpos] == T_RETURN) {
        tpos = tpos + 1;
        if (tkind[tpos] == ';') {
            outs("  MOV R0, #0\n");
        } else {
            parse_expr();
        }
        if (tkind[tpos] != ';') err("expected ;");
        tpos = tpos + 1;
        outs("  BRA .Lret_");
        outs(current);
        outs("\n");
        return;
    }
    if (tkind[tpos] == T_BREAK) {
        tpos = tpos + 1;
        if (tkind[tpos] != ';') err("expected ;");
        tpos = tpos + 1;
        if (loop_depth > 0) emit_branch("BRA", break_stack[loop_depth - 1]);
        return;
    }
    if (tkind[tpos] == T_CONTINUE) {
        tpos = tpos + 1;
        if (tkind[tpos] != ';') err("expected ;");
        tpos = tpos + 1;
        if (loop_depth > 0) emit_branch("BRA", continue_stack[loop_depth - 1]);
        return;
    }
    parse_expr_or_assign();
    if (tkind[tpos] != ';') err("expected ;");
    tpos = tpos + 1;
    (void)c;
    (void)d;
}

void parse_global(int type, char *name) {
    int size;
    int count;
    int elem;
    elem = type;
    size = 2;
    if (elem == 0) size = 1;
    if (tkind[tpos] == '[') {
        tpos = tpos + 1;
        if (tkind[tpos] != T_NUM) err("expected global size");
        count = tval[tpos];
        tpos = tpos + 1;
        if (tkind[tpos] != ']') err("expected ]");
        tpos = tpos + 1;
        size = count * size;
        add_global(name, elem + 2, elem, size);
        outs(".bss\n.align 2\n");
        outs(name);
        outs(":\n  .space ");
        outn(size);
        outs("\n.text\n");
        return;
    }
    add_global(name, elem, elem, size);
    if (tkind[tpos] == '=') {
        tpos = tpos + 1;
        if (tkind[tpos] != T_NUM) err("global init must be a number");
        outs(".data\n.align 2\n");
        outs(name);
        outs(":\n  ");
        if (elem == 0) outs(".byte ");
        else outs(".word ");
        outn(tval[tpos]);
        outs("\n.text\n");
        tpos = tpos + 1;
    } else {
        outs(".bss\n.align 2\n");
        outs(name);
        outs(":\n  .space ");
        outn(size);
        outs("\n.text\n");
    }
    if (tkind[tpos] != ';') err("expected ;");
    tpos = tpos + 1;
}

void parse_function(int type, char *name) {
    int ptype[4];
    int poff[4];
    int pcount;
    int kind;
    int off;
    char *pname;
    int i;
    (void)type;
    add_func(name);
    copy_name(current, name, 24);
    ncount = 0;
    frame_off = 2;
    pcount = 0;
    if (tkind[tpos] != '(') err("expected (");
    tpos = tpos + 1;
    if (tkind[tpos] == T_VOID) {
        tpos = tpos + 1;
    } else if (tkind[tpos] != ')') {
        while (1) {
            kind = parse_type();
            if (tkind[tpos] != T_ID) err("expected parameter");
            pname = tok_text(tpos);
            tpos = tpos + 1;
            off = 0;
            off = frame_off;
            if (kind == 1) {
                if (off % 2 != 0) off = off + 1;
                frame_off = off + 2;
            } else {
                if (off % 2 != 0) off = off + 1;
                frame_off = off + 1;
            }
            copy_name(names + ncount * 24, pname, 24);
            noff[ncount] = off;
            ntype[ncount] = kind;
            nelem[ncount] = kind;
            if (kind == 1) nsize[ncount] = 2;
            else nsize[ncount] = 1;
            ptype[pcount] = kind;
            poff[pcount] = off;
            pcount = pcount + 1;
            ncount = ncount + 1;
            if (tkind[tpos] == ',') {
                tpos = tpos + 1;
            } else {
                break;
            }
        }
    }
    if (tkind[tpos] != ')') err("expected )");
    tpos = tpos + 1;
    if (pcount > 3) err("more than three parameters");
    if (frame_off > 128) err("frame overflow");
    outs(".global ");
    outs(name);
    outs("\n");
    outs(name);
    outs(":\n");
    outs("  PUSH R4\n  MOV R4, SP\n  SUB SP, #128\n");
    i = 0;
    while (i < pcount) {
        if (ptype[i] == 0) outs("  STB [R4-");
        else outs("  ST [R4-");
        outn(poff[i]);
        outs("], R");
        outn(i + 1);
        outs("\n");
        i = i + 1;
    }
    if (tkind[tpos] != '{') err("expected {");
    tpos = tpos + 1;
    while (tkind[tpos] != '}' && tkind[tpos] != T_EOF) {
        parse_statement();
    }
    if (tkind[tpos] != '}') err("expected }");
    tpos = tpos + 1;
    outs("  MOV R0, #0\n");
    emit_ret_label();
    outs("  MOV SP, R4\n  POP R4\n  RET\n");
    if (same(name, "main")) has_main = 1;
}

void parse_program(void) {
    int type;
    char *name;
    while (tkind[tpos] != T_EOF) {
        type = parse_type();
        if (tkind[tpos] != T_ID) err("expected name");
        name = tok_text(tpos);
        tpos = tpos + 1;
        if (tkind[tpos] == '(') {
            parse_function(type, name);
        } else {
            parse_global(type, name);
        }
    }
}

void emit_externs(void) {
    int i;
    int j;
    int defined;
    i = 0;
    while (i < ccount) {
        defined = 0;
        j = 0;
        while (j < fcount) {
            if (same(calls + i * 24, funcs + j * 24)) defined = 1;
            j = j + 1;
        }
        j = 0;
        while (j < gcount) {
            if (same(calls + i * 24, gnames + j * 24)) defined = 1;
            j = j + 1;
        }
        if (!defined) {
            outs(".extern ");
            outs(calls + i * 24);
            outs("\n");
        }
        i = i + 1;
    }
}

void emit_strings(void) {
    int i;
    if (scount == 0) return;
    outs(".data\n.align 2\n");
    i = 0;
    while (i < scount) {
        outs(".Lstr");
        outn(i);
        outs(":\n  .asciz \"");
        outs(strtab + i * 80);
        outs("\"\n");
        i = i + 1;
    }
    outs(".text\n");
}

void read_source(char *path) {
    FILE *in;
    char chunk[READ_CHUNK];
    int total;
    int n;
    int i;
    in = fopen(path, "r");
    if (in == 0) {
        outs("; C-for-U error: cannot open ");
        outs(path);
        outs("\n");
        exit(1);
    }
    total = 0;
    while (1) {
        n = fread(chunk, 1, READ_CHUNK, in);
        if (n <= 0) break;
        if (total + n >= 4096) {
            fclose(in);
            err("input file too large");
        }
        i = 0;
        while (i < n) {
            source[total] = chunk[i];
            total = total + 1;
            i = i + 1;
        }
    }
    if (ferror(in)) {
        fclose(in);
        err("input read error");
    }
    fclose(in);
    source[total] = 0;
    if (total == 0) err("input file is empty");
}

void make_output_name(char *input, char *output) {
    int i;
    int dot;
    i = 0;
    dot = 0 - 1;
    while (input[i] != 0) {
        if (i >= 250) err("input path too long");
        output[i] = input[i];
        if (input[i] == '/') dot = 0 - 1;
        else if (input[i] == '.') dot = i;
        i = i + 1;
    }
    if (dot >= 0) i = dot;
    output[i] = '.';
    i = i + 1;
    output[i] = 's';
    i = i + 1;
    output[i] = 0;
}

int main(int argc, char *argv[]) {
    int argi;
    char *input;
    char *output;
    char outname[256];

    input = 0;
    output = 0;
    argi = 1;
    while (argi < argc) {
        if (same(argv[argi], "-o")) {
            if (argi + 1 >= argc) {
                outs("; C-for-U error: -o needs a filename\n");
                return 1;
            }
            output = argv[argi + 1];
            argi = argi + 2;
        } else if (input == 0) {
            input = argv[argi];
            argi = argi + 1;
        } else {
            outs("; C-for-U error: unexpected argument ");
            outs(argv[argi]);
            outs("\n");
            return 1;
        }
    }
    if (input == 0) {
        outs("; usage: C-for-U.kex <filename>.c [-o <filename>.s]\n");
        return 1;
    }
    if (output == 0) {
        make_output_name(input, outname);
        output = outname;
    }
    if (same(input, output)) {
        outs("; C-for-U error: input and output are the same file\n");
        return 1;
    }

    read_source(input);
    outfile = fopen(output, "w");
    if (outfile == 0) {
        outs("; C-for-U error: cannot create ");
        outs(output);
        outs("\n");
        return 1;
    }

    for (argi = 0; argi < argc; argi = argi + 1) {
        outs("; argv[");
        outn(argi);
        outs("] = ");
        outs(argv[argi]);
        outch(10);
    }
    tkind = (int *)malloc(MAX_TOKENS * 2);
    tval = (int *)malloc(MAX_TOKENS * 2);
    ttext = (char *)malloc(MAX_TOKENS * 32);
    tlen = (int *)malloc(MAX_TOKENS * 2);
    tcount = 0;
    tpos = 0;
    label_n = 0;
    has_main = 0;
    loop_depth = 0;
    scount = 0;
    lex_source();
    outs(".text\n");
    parse_program();
    if (has_main) {
        outs(".crt0\n.global _start\n_start:\n  CALL main\n  HLT\n.text\n");
    }
    emit_externs();
    emit_strings();
    free(tkind);
    free(tval);
    free(ttext);
    free(tlen);
    if (ferror(outfile)) err("output write error");
    fclose(outfile);
    outfile = 0;
    outs("; wrote ");
    outs(output);
    outs("\n");
    return 0;
}

















