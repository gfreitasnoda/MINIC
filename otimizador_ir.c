#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>

typedef __int128 oi128;

//Arena de memoria liberada ao fim de cada operacao publica
typedef struct OtBloco { struct OtBloco *prox; size_t usado, cap; _Alignas(16) char dados[]; } OtBloco;
static OtBloco *ot_blocos;

static void *ot_alloc(size_t n) {
    n = (n + 15) & ~(size_t)15;
    if (!ot_blocos || ot_blocos->usado + n > ot_blocos->cap) {
        size_t cap = n > 65536 ? n : 65536;
        OtBloco *b = calloc(1, sizeof(OtBloco) + cap);
        if (!b) { fputs("Erro: memoria insuficiente\n", stderr); exit(1); }
        b->prox = ot_blocos; b->usado = 0; b->cap = cap;
        ot_blocos = b;
    }
    void *p = ot_blocos->dados + ot_blocos->usado;
    ot_blocos->usado += n;
    return p;
}

static void ot_liberar(void) {
    while (ot_blocos) { OtBloco *p = ot_blocos->prox; free(ot_blocos); ot_blocos = p; }
}

static const char *ot_dup_n(const char *s, size_t n) {
    char *r = ot_alloc(n + 1);
    memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

static const char *ot_dup(const char *s) { return ot_dup_n(s, strlen(s)); }

static const char *ot_fmt(const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *r = ot_alloc((size_t)n + 1);
    vsnprintf(r, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return r;
}

//Falha equivalente a KeyError/RuntimeError
static jmp_buf *ot_handler;

static void ot_falha(void) {
    if (ot_handler) longjmp(*ot_handler, 1);
    fputs("Erro: falha interna do otimizador\n", stderr);
    exit(1);
}

//Texto
static int ot_isspace(unsigned char c) { return c == ' ' || (c >= 9 && c <= 13) || (c >= 0x1c && c <= 0x1f); }
static int ot_isdigit(unsigned char c) { return c >= '0' && c <= '9'; }
static int ot_eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }
static int ot_eq0(const char *a, const char *b) { return (a == NULL || b == NULL) ? a == b : strcmp(a, b) == 0; }

static int ot_startswith(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }

static int ot_endswith(const char *s, const char *p) {
    size_t a = strlen(s), b = strlen(p);
    return a >= b && memcmp(s + a - b, p, b) == 0;
}

//Remove espacos nas pontas
static const char *ot_strip(const char *s) {
    size_t i = 0, j = strlen(s);
    while (i < j && ot_isspace((unsigned char)s[i])) i++;
    while (j > i && ot_isspace((unsigned char)s[j - 1])) j--;
    return ot_dup_n(s + i, j - i);
}

//Token sem espacos a partir de s; devolve o tamanho
static size_t ot_tok(const char *s) {
    size_t n = 0;
    while (s[n] && !ot_isspace((unsigned char)s[n])) n++;
    return n;
}

static int ot_todo_sem_espaco(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) if (ot_isspace((unsigned char)s[i])) return 0;
    return 1;
}

//Padroes numericos
static int ot_re_int(const char *x) {
    if (*x == '-') x++;
    if (!ot_isdigit((unsigned char)*x)) return 0;
    while (ot_isdigit((unsigned char)*x)) x++;
    return *x == '\0';
}

static int ot_re_num(const char *x) {
    if (*x == '-') x++;
    int d1 = 0;
    while (ot_isdigit((unsigned char)*x)) { x++; d1++; }
    if (d1 > 0) {
        if (*x == '.') { x++; while (ot_isdigit((unsigned char)*x)) x++; }
    } else {
        if (*x != '.') return 0;
        x++;
        int d2 = 0;
        while (ot_isdigit((unsigned char)*x)) { x++; d2++; }
        if (!d2) return 0;
    }
    if (*x == 'e' || *x == 'E') {
        x++;
        if (*x == '+' || *x == '-') x++;
        if (!ot_isdigit((unsigned char)*x)) return 0;
        while (ot_isdigit((unsigned char)*x)) x++;
    }
    return *x == '\0';
}

static int ot_re_temp(const char *x) {
    if (*x != 't') return 0;
    x++;
    if (!ot_isdigit((unsigned char)*x)) return 0;
    while (ot_isdigit((unsigned char)*x)) x++;
    return *x == '\0';
}

//Inteiros de 128 bits; falha se nao couber
static int ot_int(const char *x, oi128 *out) {
    int neg = 0;
    if (*x == '-') { neg = 1; x++; }
    oi128 v = 0;
    int dig = 0;
    for (; *x; x++) {
        if (++dig > 36) return 0;
        v = v * 10 + (*x - '0');
    }
    *out = neg ? -v : v;
    return 1;
}

static const char *ot_i2s(oi128 v) {
    char buf[64];
    int p = 63, neg = v < 0;
    buf[p] = '\0';
    unsigned __int128 u = neg ? (unsigned __int128)(-(v + 1)) + 1 : (unsigned __int128)v;
    if (u == 0) buf[--p] = '0';
    while (u) { buf[--p] = (char)('0' + (int)(u % 10)); u /= 10; }
    if (neg) buf[--p] = '-';
    return ot_dup(buf + p);
}

#define OT_INT_MIN (-((oi128)1 << 31))
#define OT_INT_MAX (((oi128)1 << 31) - 1)

static int ot_cabe(oi128 v) { return v >= OT_INT_MIN && v <= OT_INT_MAX; }

static int ot_eh_lit(const char *x) {
    return ot_re_num(x) || ot_eq(x, "true") || ot_eq(x, "false") || x[0] == '"' || x[0] == '\'';
}

//Conjuntos de nomes
typedef struct { const char **v; int n, cap; } OtSet;

static int ot_set_has(const OtSet *s, const char *x) {
    for (int i = 0; i < s->n; i++) if (strcmp(s->v[i], x) == 0) return 1;
    return 0;
}

static void ot_set_add(OtSet *s, const char *x) {
    if (ot_set_has(s, x)) return;
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 8;
        const char **nv = ot_alloc((size_t)s->cap * sizeof(char *));
        if (s->n) memcpy(nv, s->v, (size_t)s->n * sizeof(char *));
        s->v = nv;
    }
    s->v[s->n++] = x;
}

static void ot_set_del(OtSet *s, const char *x) {
    for (int i = 0; i < s->n; i++) {
        if (strcmp(s->v[i], x) == 0) { s->v[i] = s->v[--s->n]; return; }
    }
}

static void ot_set_union(OtSet *dst, const OtSet *src) {
    for (int i = 0; i < src->n; i++) ot_set_add(dst, src->v[i]);
}

static int ot_set_eq(const OtSet *a, const OtSet *b) {
    if (a->n != b->n) return 0;
    for (int i = 0; i < a->n; i++) if (!ot_set_has(b, a->v[i])) return 0;
    return 1;
}

static OtSet ot_set_copy(const OtSet *s) {
    OtSet r = {0};
    ot_set_union(&r, s);
    return r;
}

//Dicionarios nome -> valor
typedef struct { const char **k, **v; int n, cap; } OtMap;

static int ot_map_find(const OtMap *m, const char *k) {
    for (int i = 0; i < m->n; i++) if (strcmp(m->k[i], k) == 0) return i;
    return -1;
}

static const char *ot_map_get(const OtMap *m, const char *k) {
    int i = ot_map_find(m, k);
    return i < 0 ? NULL : m->v[i];
}

static void ot_map_put(OtMap *m, const char *k, const char *v) {
    int i = ot_map_find(m, k);
    if (i >= 0) { m->v[i] = v; return; }
    if (m->n == m->cap) {
        m->cap = m->cap ? m->cap * 2 : 8;
        const char **nk = ot_alloc((size_t)m->cap * sizeof(char *));
        const char **nv = ot_alloc((size_t)m->cap * sizeof(char *));
        if (m->n) { memcpy(nk, m->k, (size_t)m->n * sizeof(char *)); memcpy(nv, m->v, (size_t)m->n * sizeof(char *)); }
        m->k = nk; m->v = nv;
    }
    m->k[m->n] = k; m->v[m->n++] = v;
}

static void ot_map_del_idx(OtMap *m, int i) {
    m->n--;
    m->k[i] = m->k[m->n];
    m->v[i] = m->v[m->n];
}

static OtMap ot_map_copy(const OtMap *m) {
    OtMap r = {0};
    for (int i = 0; i < m->n; i++) ot_map_put(&r, m->k[i], m->v[i]);
    return r;
}

static int ot_map_eq(const OtMap *a, const OtMap *b) {
    if (a->n != b->n) return 0;
    for (int i = 0; i < a->n; i++) {
        const char *v = ot_map_get(b, a->k[i]);
        if (!v || strcmp(v, a->v[i]) != 0) return 0;
    }
    return 1;
}

//Instrucoes: s[i] repete a posicao i da tupla
typedef enum { K_LABEL, K_COPY, K_BIN, K_UN, K_LOAD, K_STORE, K_IFFALSE, K_GOTO,
               K_PRINT, K_READ, K_READIDX, K_RET, K_PARAM, K_CALL } OtKind;

typedef struct { int k; const char *s[5]; } OtIns;
typedef struct { OtIns *v; int n, cap; } OtLista;

static OtLista *ot_lista_nova(void) { return ot_alloc(sizeof(OtLista)); }

static void ot_lista_add(OtLista *l, OtIns i) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        OtIns *nv = ot_alloc((size_t)l->cap * sizeof(OtIns));
        if (l->n) memcpy(nv, l->v, (size_t)l->n * sizeof(OtIns));
        l->v = nv;
    }
    l->v[l->n++] = i;
}

static OtIns ot_mk(int k, const char *a, const char *b, const char *c, const char *d) {
    OtIns i;
    memset(&i, 0, sizeof i);
    i.k = k; i.s[1] = a; i.s[2] = b; i.s[3] = c; i.s[4] = d;
    return i;
}

static int ot_ins_eq(const OtIns *a, const OtIns *b) {
    if (a->k != b->k) return 0;
    for (int i = 1; i < 5; i++) if (!ot_eq0(a->s[i], b->s[i])) return 0;
    return 1;
}

static int ot_lista_eq(const OtLista *a, const OtLista *b) {
    if (a == b) return 1;
    if (a->n != b->n) return 0;
    for (int i = 0; i < a->n; i++) if (!ot_ins_eq(&a->v[i], &b->v[i])) return 0;
    return 1;
}

//Parse
static int ot_comutativo(const char *op) {
    return ot_eq(op, "+") || ot_eq(op, "*") || ot_eq(op, "==") || ot_eq(op, "!=")
           || ot_eq(op, "and") || ot_eq(op, "or");
}

//Casa base[idx] com a base minima; idx sem espacos ate o ] final
static int ot_base_idx(const char *s, size_t n, const char **base, const char **idx) {
    for (size_t p = 1; p < n; p++) {
        if (ot_isspace((unsigned char)s[p - 1])) return 0;
        if (s[p] != '[') continue;
        size_t resto = n - p - 1;
        if (resto >= 2 && s[n - 1] == ']' && ot_todo_sem_espaco(s + p + 1, resto)) {
            *base = ot_dup_n(s, p);
            *idx = ot_dup_n(s + p + 1, resto - 1);
            return 1;
        }
    }
    return 0;
}

//Casa "call f, N" a partir de t
static int ot_re_call(const char *t, const char **f, const char **n) {
    if (!ot_startswith(t, "call ")) return 0;
    const char *r = t + 5;
    size_t tk = ot_tok(r);
    if (tk < 2 || r[tk - 1] != ',' || r[tk] != ' ') return 0;
    const char *d = r + tk + 1;
    if (!*d) return 0;
    for (const char *q = d; *q; q++) if (!ot_isdigit((unsigned char)*q)) return 0;
    *f = ot_dup_n(r, tk - 1);
    *n = ot_dup(d);
    return 1;
}

//Devolve 0 se a linha nao e reconhecida
static int ot_parse_rhs(const char *dst, const char *rhs, OtIns *out) {
    if (rhs[0] == '"' || rhs[0] == '\'') { *out = ot_mk(K_COPY, dst, rhs, NULL, NULL); return 1; }
    int np = 1;
    for (const char *q = rhs; *q; q++) if (*q == ' ') np++;
    if (np == 1) {
        if (ot_eh_lit(rhs)) { *out = ot_mk(K_COPY, dst, rhs, NULL, NULL); return 1; }
        if (rhs[0] == '-') { *out = ot_mk(K_UN, dst, "-", rhs + 1, NULL); return 1; }
        *out = ot_mk(K_COPY, dst, rhs, NULL, NULL);
        return 1;
    }
    const char *p[3];
    int cnt = 0;
    const char *ini = rhs;
    for (const char *q = rhs;; q++) {
        if (*q == ' ' || *q == '\0') {
            if (cnt < 3) p[cnt] = ot_dup_n(ini, (size_t)(q - ini));
            cnt++;
            ini = q + 1;
            if (*q == '\0') break;
        }
    }
    if (cnt == 2 && ot_eq(p[0], "not")) { *out = ot_mk(K_UN, dst, "not", p[1], NULL); return 1; }
    if (cnt == 3) { *out = ot_mk(K_BIN, dst, p[0], p[1], p[2]); return 1; }
    return 0;
}

static int ot_parse(const char *txt, OtIns *out) {
    const char *t = ot_strip(txt);
    size_t n = strlen(t), tk = ot_tok(t);
    const char *a, *b;
    if (n >= 2 && t[n - 1] == ':' && tk == n) { *out = ot_mk(K_LABEL, ot_dup_n(t, n - 1), NULL, NULL, NULL); return 1; }
    if (ot_startswith(t, "ifFalse ")) {
        const char *r = t + 8;
        size_t c = ot_tok(r);
        if (c > 0 && ot_startswith(r + c, " goto ")) {
            const char *l = r + c + 6;
            size_t ln = ot_tok(l);
            if (ln > 0 && l[ln] == '\0') {
                *out = ot_mk(K_IFFALSE, ot_dup_n(r, c), ot_dup_n(l, ln), NULL, NULL);
                return 1;
            }
        }
    }
    if (ot_startswith(t, "goto ")) {
        size_t ln = ot_tok(t + 5);
        if (ln > 0 && t[5 + ln] == '\0') { *out = ot_mk(K_GOTO, ot_dup_n(t + 5, ln), NULL, NULL, NULL); return 1; }
    }
    if (ot_eq(t, "return")) { *out = ot_mk(K_RET, NULL, NULL, NULL, NULL); return 1; }
    if (ot_startswith(t, "return ") && n > 7) { *out = ot_mk(K_RET, ot_dup(t + 7), NULL, NULL, NULL); return 1; }
    if (ot_startswith(t, "print ") && n > 6) { *out = ot_mk(K_PRINT, ot_dup(t + 6), NULL, NULL, NULL); return 1; }
    if (ot_startswith(t, "param ") && n > 6) { *out = ot_mk(K_PARAM, ot_dup(t + 6), NULL, NULL, NULL); return 1; }
    if (ot_startswith(t, "read ")) {
        size_t ln = ot_tok(t + 5);
        if (ln > 0 && t[5 + ln] == '\0') {
            if (ot_base_idx(t + 5, ln, &a, &b)) { *out = ot_mk(K_READIDX, a, b, NULL, NULL); return 1; }
            *out = ot_mk(K_READ, ot_dup_n(t + 5, ln), NULL, NULL, NULL);
            return 1;
        }
    }
    if (ot_startswith(t, "store ")) {
        const char *r = t + 6;
        size_t c = ot_tok(r);
        if (c >= 2 && r[c - 1] == ',' && r[c] == ' ' && r[c + 1] != '\0'
            && ot_base_idx(r, c - 1, &a, &b)) {
            *out = ot_mk(K_STORE, a, b, ot_dup(r + c + 1), NULL);
            return 1;
        }
    }
    if (ot_re_call(t, &a, &b)) { *out = ot_mk(K_CALL, NULL, a, b, NULL); return 1; }
    if (tk > 0 && ot_startswith(t + tk, " = ") && t[tk + 3] != '\0') {
        const char *d = ot_dup_n(t, tk), *rhs = t + tk + 3;
        if (ot_startswith(rhs, "load ")) {
            size_t ln = ot_tok(rhs + 5);
            if (ln > 0 && rhs[5 + ln] == '\0' && ot_base_idx(rhs + 5, ln, &a, &b)) {
                *out = ot_mk(K_LOAD, d, a, b, NULL);
                return 1;
            }
        }
        if (ot_re_call(rhs, &a, &b)) { *out = ot_mk(K_CALL, d, a, b, NULL); return 1; }
        return ot_parse_rhs(d, rhs, out);
    }
    return 0;
}

static const char *ot_fmt_ins(const OtIns *i) {
    switch (i->k) {
    case K_LABEL:   return ot_fmt("%s:", i->s[1]);
    case K_COPY:    return ot_fmt("  %s = %s", i->s[1], i->s[2]);
    case K_BIN:     return ot_fmt("  %s = %s %s %s", i->s[1], i->s[2], i->s[3], i->s[4]);
    case K_UN:      return ot_eq(i->s[2], "-") ? ot_fmt("  %s = -%s", i->s[1], i->s[3])
                                               : ot_fmt("  %s = not %s", i->s[1], i->s[3]);
    case K_LOAD:    return ot_fmt("  %s = load %s[%s]", i->s[1], i->s[2], i->s[3]);
    case K_STORE:   return ot_fmt("  store %s[%s], %s", i->s[1], i->s[2], i->s[3]);
    case K_IFFALSE: return ot_fmt("  ifFalse %s goto %s", i->s[1], i->s[2]);
    case K_GOTO:    return ot_fmt("  goto %s", i->s[1]);
    case K_PRINT:   return ot_fmt("  print %s", i->s[1]);
    case K_READ:    return ot_fmt("  read %s", i->s[1]);
    case K_READIDX: return ot_fmt("  read %s[%s]", i->s[1], i->s[2]);
    case K_RET:     return i->s[1] ? ot_fmt("  return %s", i->s[1]) : "  return";
    case K_PARAM:   return ot_fmt("  param %s", i->s[1]);
    default:        return i->s[1] ? ot_fmt("  %s = call %s, %s", i->s[1], i->s[2], i->s[3])
                                   : ot_fmt("  call %s, %s", i->s[2], i->s[3]);
    }
}

//Operandos usados (sem literais) e nome definido
static int ot_usos(const OtIns *i, const char **ops) {
    const char *c[2];
    int n = 0;
    switch (i->k) {
    case K_COPY:    c[n++] = i->s[2]; break;
    case K_BIN:     c[n++] = i->s[2]; c[n++] = i->s[4]; break;
    case K_UN:      c[n++] = i->s[3]; break;
    case K_LOAD:    c[n++] = i->s[3]; break;
    case K_STORE:   c[n++] = i->s[2]; c[n++] = i->s[3]; break;
    case K_IFFALSE: c[n++] = i->s[1]; break;
    case K_PRINT: case K_PARAM: c[n++] = i->s[1]; break;
    case K_RET:     if (i->s[1]) c[n++] = i->s[1]; break;
    case K_READIDX: c[n++] = i->s[2]; break;
    default: break;
    }
    int m = 0;
    for (int j = 0; j < n; j++) if (!ot_eh_lit(c[j])) ops[m++] = c[j];
    return m;
}

static int ot_usa(const OtIns *i, const char *nome) {
    const char *ops[2];
    int n = ot_usos(i, ops);
    for (int j = 0; j < n; j++) if (strcmp(ops[j], nome) == 0) return 1;
    return 0;
}

static const char *ot_defs(const OtIns *i) {
    switch (i->k) {
    case K_COPY: case K_BIN: case K_UN: case K_LOAD: case K_READ: case K_CALL: return i->s[1];
    default: return NULL;
    }
}

//CFG
typedef struct { int a, b; } OtBlocoCfg;
typedef struct { int n; int s[2]; } OtSuc;
typedef struct { OtBlocoCfg *bl; OtSuc *su; int n; } OtCfg;

static OtCfg ot_construir_cfg(const OtLista *ins) {
    OtCfg c = {0};
    if (ins->n == 0) return c;
    c.bl = ot_alloc((size_t)ins->n * sizeof(OtBlocoCfg));
    int ini = 0;
    for (int i = 0; i < ins->n; i++) {
        if (i > ini) {
            int ult = ins->v[i - 1].k;
            if (ins->v[i].k == K_LABEL || ult == K_GOTO || ult == K_IFFALSE || ult == K_RET) {
                c.bl[c.n].a = ini; c.bl[c.n].b = i; c.n++;
                ini = i;
            }
        }
    }
    c.bl[c.n].a = ini; c.bl[c.n].b = ins->n; c.n++;
    c.su = ot_alloc((size_t)c.n * sizeof(OtSuc));
    for (int n = 0; n < c.n; n++) {
        const OtIns *u = &ins->v[c.bl[n].b - 1];
        OtSuc *s = &c.su[n];
        s->n = 0;
        int alvo = -1;
        const char *rot = u->k == K_GOTO ? u->s[1] : u->k == K_IFFALSE ? u->s[2] : NULL;
        if (rot) {
            for (int m = 0; m < c.n; m++) {
                const OtIns *p = &ins->v[c.bl[m].a];
                if (p->k == K_LABEL && strcmp(p->s[1], rot) == 0) alvo = m;
            }
            if (alvo < 0) ot_falha();
        }
        if (u->k == K_GOTO) { s->s[s->n++] = alvo; }
        else if (u->k == K_IFFALSE) {
            s->s[s->n++] = alvo;
            if (n + 1 < c.n) s->s[s->n++] = n + 1;
        } else if (u->k == K_RET) { }
        else if (n + 1 < c.n) s->s[s->n++] = n + 1;
    }
    return c;
}

//Propagacao de constantes e copias
typedef enum { COPIA_NENHUMA, COPIA_TODAS, COPIA_TEMPS } OtModoCopia;
typedef struct { int cons, alg; int copia; } OtCfgOpt;

static const OtCfgOpt OT_CFG_TOTAL = { 1, 1, COPIA_TODAS };
static const OtCfgOpt OT_CFG_CONST = { 1, 1, COPIA_NENHUMA };
static const OtCfgOpt OT_CFG_ALG = { 0, 1, COPIA_TODAS };
static const OtCfgOpt OT_CFG_COPIA = { 0, 0, COPIA_TODAS };
static const OtCfgOpt OT_CFG_CSE = { 0, 0, COPIA_TEMPS };

static const char *ot_subst(const char *op, const OtMap *st) {
    if (ot_eh_lit(op)) return op;
    const char *v = ot_map_get(st, op);
    return v ? v : op;
}

static void ot_kill(OtMap *st, const char *nome) {
    int i = ot_map_find(st, nome);
    if (i >= 0) ot_map_del_idx(st, i);
    for (i = 0; i < st->n;) {
        if (strcmp(st->v[i], nome) == 0) ot_map_del_idx(st, i);
        else i++;
    }
}

static void ot_def(OtMap *st, const char *d, const char *val) {
    ot_kill(st, d);
    if (val != NULL && strcmp(val, d) != 0) ot_map_put(st, d, val);
}

//Divisao com truncamento em direcao a zero
static oi128 ot_div(oi128 a, oi128 b) {
    oi128 aa = a < 0 ? -a : a, bb = b < 0 ? -b : b;
    oi128 q = aa / bb;
    return ((a >= 0) == (b >= 0)) ? q : -q;
}

//Dobra de constantes int/bool; NULL se nao dobra
static const char *ot_fold_bin(const char *op, const char *a, const char *b) {
    if (ot_re_int(a) && ot_re_int(b)) {
        oi128 x, y;
        if (!ot_int(a, &x) || !ot_int(b, &y)) return NULL;
        if (ot_eq(op, "+") || ot_eq(op, "-") || ot_eq(op, "*")) {
            oi128 r = ot_eq(op, "+") ? x + y : ot_eq(op, "-") ? x - y : x * y;
            return ot_cabe(r) ? ot_i2s(r) : NULL;
        }
        if (ot_eq(op, "/") && y != 0) return ot_i2s(ot_div(x, y));
        if (ot_eq(op, "%") && y != 0) return ot_i2s(x - y * ot_div(x, y));
        if (ot_eq(op, "<")) return x < y ? "true" : "false";
        if (ot_eq(op, "<=")) return x <= y ? "true" : "false";
        if (ot_eq(op, ">")) return x > y ? "true" : "false";
        if (ot_eq(op, ">=")) return x >= y ? "true" : "false";
        if (ot_eq(op, "==")) return x == y ? "true" : "false";
        if (ot_eq(op, "!=")) return x != y ? "true" : "false";
        return NULL;
    }
    int ba = ot_eq(a, "true") || ot_eq(a, "false"), bb = ot_eq(b, "true") || ot_eq(b, "false");
    if (ba && bb) {
        int x = ot_eq(a, "true"), y = ot_eq(b, "true");
        if (ot_eq(op, "==")) return x == y ? "true" : "false";
        if (ot_eq(op, "!=")) return x != y ? "true" : "false";
        if (ot_eq(op, "and")) return (x && y) ? "true" : "false";
        if (ot_eq(op, "or")) return (x || y) ? "true" : "false";
    }
    return NULL;
}

//Identidades algebricas; devolve operando ou NULL
static const char *ot_alg_bin(const char *op, const char *a, const char *b) {
    if (ot_eq(op, "+")) {
        if (ot_eq(b, "0")) return a;
        if (ot_eq(a, "0")) return b;
    } else if (ot_eq(op, "-")) {
        if (ot_eq(b, "0")) return a;
    } else if (ot_eq(op, "*")) {
        if (ot_eq(b, "1")) return a;
        if (ot_eq(a, "1")) return b;
    } else if (ot_eq(op, "/")) {
        if (ot_eq(b, "1")) return a;
    } else if (ot_eq(op, "and")) {
        if (ot_eq(a, "false") || ot_eq(b, "false")) return "false";
        if (ot_eq(a, "true")) return b;
        if (ot_eq(b, "true")) return a;
    } else if (ot_eq(op, "or")) {
        if (ot_eq(a, "true") || ot_eq(b, "true")) return "true";
        if (ot_eq(a, "false")) return b;
        if (ot_eq(b, "false")) return a;
    }
    return NULL;
}

static const char *ot_simp_bin(const char *op, const char *a, const char *b, const OtCfgOpt *cfg) {
    const char *r = cfg->cons ? ot_fold_bin(op, a, b) : NULL;
    if (r == NULL && cfg->alg) r = ot_alg_bin(op, a, b);
    return r;
}

static const char *ot_simp_un(const char *op, const char *a) {
    if (ot_eq(op, "-") && ot_re_int(a)) {
        oi128 x;
        if (!ot_int(a, &x)) return NULL;
        return ot_cabe(-x) ? ot_i2s(-x) : NULL;
    }
    if (ot_eq(op, "not") && (ot_eq(a, "true") || ot_eq(a, "false"))) return ot_eq(a, "true") ? "false" : "true";
    return NULL;
}

//O valor v pode ser lembrado como conteudo de d?
static const char *ot_guardar(const OtCfgOpt *cfg, const char *d, const char *v) {
    if (v == NULL) return NULL;
    if (ot_eh_lit(v)) return cfg->cons ? v : NULL;
    if (cfg->copia == COPIA_TODAS || (cfg->copia == COPIA_TEMPS && ot_re_temp(d))) return v;
    return NULL;
}

//Transforma uma instrucao e atualiza o estado; devolve 0 se removida
static int ot_passo(const OtIns *i, OtMap *st, const OtSet *glob, const OtCfgOpt *cfg, OtIns *out) {
    switch (i->k) {
    case K_COPY: {
        const char *s = ot_subst(i->s[2], st);
        if (strcmp(s, i->s[1]) == 0) return 0;
        ot_def(st, i->s[1], ot_guardar(cfg, i->s[1], s));
        *out = ot_mk(K_COPY, i->s[1], s, NULL, NULL);
        return 1;
    }
    case K_BIN: {
        const char *d = i->s[1], *op = i->s[3];
        const char *a = ot_subst(i->s[2], st), *b = ot_subst(i->s[4], st);
        const char *r = ot_simp_bin(op, a, b, cfg);
        if (r != NULL) {
            if (strcmp(r, d) == 0) return 0;
            ot_def(st, d, ot_guardar(cfg, d, r));
            *out = ot_mk(K_COPY, d, r, NULL, NULL);
            return 1;
        }
        ot_def(st, d, NULL);
        *out = ot_mk(K_BIN, d, a, op, b);
        return 1;
    }
    case K_UN: {
        const char *d = i->s[1], *op = i->s[2];
        const char *a = ot_subst(i->s[3], st);
        const char *r = cfg->cons ? ot_simp_un(op, a) : NULL;
        if (r != NULL) {
            ot_def(st, d, ot_guardar(cfg, d, r));
            *out = ot_mk(K_COPY, d, r, NULL, NULL);
            return 1;
        }
        ot_def(st, d, NULL);
        *out = ot_mk(K_UN, d, op, a, NULL);
        return 1;
    }
    case K_LOAD: {
        const char *idx = ot_subst(i->s[3], st);
        ot_def(st, i->s[1], NULL);
        *out = ot_mk(K_LOAD, i->s[1], i->s[2], idx, NULL);
        return 1;
    }
    case K_STORE:
        *out = ot_mk(K_STORE, i->s[1], ot_subst(i->s[2], st), ot_subst(i->s[3], st), NULL);
        return 1;
    case K_IFFALSE: {
        const char *c = ot_subst(i->s[1], st);
        if (cfg->cons && ot_eq(c, "true")) return 0;
        if (cfg->cons && ot_eq(c, "false")) { *out = ot_mk(K_GOTO, i->s[2], NULL, NULL, NULL); return 1; }
        *out = ot_mk(K_IFFALSE, c, i->s[2], NULL, NULL);
        return 1;
    }
    case K_PRINT: case K_PARAM:
        *out = ot_mk(i->k, ot_subst(i->s[1], st), NULL, NULL, NULL);
        return 1;
    case K_RET:
        *out = ot_mk(K_RET, i->s[1] ? ot_subst(i->s[1], st) : NULL, NULL, NULL, NULL);
        return 1;
    case K_READ:
        ot_def(st, i->s[1], NULL);
        *out = *i;
        return 1;
    case K_READIDX:
        *out = ot_mk(K_READIDX, i->s[1], ot_subst(i->s[2], st), NULL, NULL);
        return 1;
    case K_CALL:
        if (i->s[1]) ot_def(st, i->s[1], NULL);
        for (int j = 0; j < st->n;) {
            if (ot_set_has(glob, st->k[j]) || ot_set_has(glob, st->v[j])) ot_map_del_idx(st, j);
            else j++;
        }
        *out = *i;
        return 1;
    default:
        *out = *i;
        return 1;
    }
}

static OtLista *ot_propagar(OtLista *instrs, const OtSet *glob, const OtCfgOpt *cfg) {
    OtCfg c = ot_construir_cfg(instrs);
    if (c.n == 0) return instrs;
    int n = c.n;
    int **preds = ot_alloc((size_t)n * sizeof(int *));
    int *npreds = ot_alloc((size_t)n * sizeof(int));
    for (int b = 0; b < n; b++) { preds[b] = ot_alloc((size_t)(2 * n + 2) * sizeof(int)); npreds[b] = 0; }
    for (int b = 0; b < n; b++)
        for (int k = 0; k < c.su[b].n; k++) { int s = c.su[b].s[k]; preds[s][npreds[s]++] = b; }
    OtMap **ent = ot_alloc((size_t)n * sizeof(OtMap *));
    OtMap **sai = ot_alloc((size_t)n * sizeof(OtMap *));
    for (int b = 0; b < n; b++) ent[b] = sai[b] = NULL;
    int convergiu = 0;
    for (int it = 0; it < 200; it++) {
        int mudou = 0;
        for (int b = 0; b < n; b++) {
            OtMap e = {0};
            if (b != 0) {
                int primeiro = 1;
                for (int k = 0; k < npreds[b]; k++) {
                    OtMap *o = sai[preds[b][k]];
                    if (!o) continue;
                    if (primeiro) { e = ot_map_copy(o); primeiro = 0; continue; }
                    for (int q = 0; q < e.n;) {
                        const char *ov = ot_map_get(o, e.k[q]);
                        if (!ov || strcmp(ov, e.v[q]) != 0) ot_map_del_idx(&e, q);
                        else q++;
                    }
                }
                if (primeiro) continue;
            }
            OtMap st = ot_map_copy(&e);
            for (int p = c.bl[b].a; p < c.bl[b].b; p++) {
                OtIns tmp;
                ot_passo(&instrs->v[p], &st, glob, cfg, &tmp);
            }
            if (!ent[b] || !ot_map_eq(&e, ent[b]) || !ot_map_eq(&st, sai[b])) {
                ent[b] = ot_alloc(sizeof(OtMap)); *ent[b] = e;
                sai[b] = ot_alloc(sizeof(OtMap)); *sai[b] = st;
                mudou = 1;
            }
        }
        if (!mudou) { convergiu = 1; break; }
    }
    if (!convergiu) return instrs;    //sem convergencia: nao arrisca
    OtLista *novos = ot_lista_nova();
    for (int b = 0; b < n; b++) {
        if (!ent[b]) {
            for (int p = c.bl[b].a; p < c.bl[b].b; p++) ot_lista_add(novos, instrs->v[p]);
            continue;
        }
        OtMap st = ot_map_copy(ent[b]);
        for (int p = c.bl[b].a; p < c.bl[b].b; p++) {
            OtIns r;
            if (ot_passo(&instrs->v[p], &st, glob, cfg, &r)) ot_lista_add(novos, r);
        }
    }
    return novos;
}

//CSE local
typedef struct { int bin; const char *op, *x, *y; const char *h; } OtAv;
typedef struct { OtAv *v; int n, cap; } OtAvs;

static int ot_av_find(const OtAvs *av, int bin, const char *op, const char *x, const char *y) {
    for (int i = 0; i < av->n; i++) {
        const OtAv *e = &av->v[i];
        if (e->bin == bin && strcmp(e->op, op) == 0 && strcmp(e->x, x) == 0 && ot_eq0(e->y, y)) return i;
    }
    return -1;
}

static int ot_av_operando(const OtAv *e, const char *nome) {
    return strcmp(e->x, nome) == 0 || (e->y && strcmp(e->y, nome) == 0);
}

static void ot_av_kill(OtAvs *av, const char *nome) {
    for (int i = 0; i < av->n;) {
        if (strcmp(av->v[i].h, nome) == 0 || ot_av_operando(&av->v[i], nome)) av->v[i] = av->v[--av->n];
        else i++;
    }
}

static void ot_av_put(OtAvs *av, OtAv e) {
    int i = ot_av_find(av, e.bin, e.op, e.x, e.y);
    if (i >= 0) { av->v[i].h = e.h; return; }
    if (av->n == av->cap) {
        av->cap = av->cap ? av->cap * 2 : 16;
        OtAv *nv = ot_alloc((size_t)av->cap * sizeof(OtAv));
        if (av->n) memcpy(nv, av->v, (size_t)av->n * sizeof(OtAv));
        av->v = nv;
    }
    av->v[av->n++] = e;
}

static OtLista *ot_cse_local(OtLista *instrs, const OtSet *glob) {
    OtAvs av = {0};
    OtLista *out = ot_lista_nova();
    for (int p = 0; p < instrs->n; p++) {
        const OtIns *ins = &instrs->v[p];
        if (ins->k == K_LABEL) {
            av.n = 0;
            ot_lista_add(out, *ins);
            continue;
        }
        if (ins->k == K_BIN || ins->k == K_UN) {
            const char *d = ins->s[1];
            OtAv key;
            int tem_key = 0;
            memset(&key, 0, sizeof key);
            if (ins->k == K_BIN) {
                const char *a = ins->s[2], *op = ins->s[3], *b = ins->s[4];
                if (!(ot_eh_lit(a) && ot_eh_lit(b))) {
                    const char *x = a, *y = b;
                    if (ot_comutativo(op) && strcmp(a, b) > 0) { x = b; y = a; }
                    key.bin = 1; key.op = op; key.x = x; key.y = y;
                    tem_key = 1;
                }
            } else {
                key.bin = 0; key.op = ins->s[2]; key.x = ins->s[3]; key.y = NULL;
                tem_key = 1;
            }
            int idx = tem_key ? ot_av_find(&av, key.bin, key.op, key.x, key.y) : -1;
            if (idx >= 0 && strcmp(av.v[idx].h, d) != 0) {
                ot_av_kill(&av, d);
                int j = ot_av_find(&av, key.bin, key.op, key.x, key.y);
                if (j < 0) ot_falha();    //entrada removida pelo kill
                ot_lista_add(out, ot_mk(K_COPY, d, av.v[j].h, NULL, NULL));
                continue;
            }
            ot_av_kill(&av, d);
            if (tem_key && !ot_av_operando(&key, d)) {
                key.h = d;
                ot_av_put(&av, key);
            }
            ot_lista_add(out, *ins);
            continue;
        }
        const char *d = ot_defs(ins);
        if (d != NULL) ot_av_kill(&av, d);
        if (ins->k == K_CALL) {
            for (int i = 0; i < av.n;) {
                if (ot_set_has(glob, av.v[i].h) || ot_set_has(glob, av.v[i].x)
                    || (av.v[i].y && ot_set_has(glob, av.v[i].y))) av.v[i] = av.v[--av.n];
                else i++;
            }
        }
        ot_lista_add(out, *ins);
    }
    return out;
}

//Fluxo de controle
static OtLista *ot_limpar_fluxo(OtLista *instrs) {
    OtCfg c = ot_construir_cfg(instrs);
    if (c.n == 0) return instrs;
    char *alc = ot_alloc((size_t)c.n);
    int *pilha = ot_alloc((size_t)(c.n * 2 + 2) * sizeof(int));
    int sp = 0;
    memset(alc, 0, (size_t)c.n);
    alc[0] = 1; pilha[sp++] = 0;
    while (sp) {
        int b = pilha[--sp];
        for (int k = 0; k < c.su[b].n; k++) {
            int s = c.su[b].s[k];
            if (!alc[s]) { alc[s] = 1; pilha[sp++] = s; }
        }
    }
    OtLista *out = ot_lista_nova();
    for (int b = 0; b < c.n; b++)
        if (alc[b]) for (int p = c.bl[b].a; p < c.bl[b].b; p++) ot_lista_add(out, instrs->v[p]);
    for (;;) {
        OtSet alvos = {0};
        for (int i = 0; i < out->n; i++) {
            if (out->v[i].k == K_IFFALSE) ot_set_add(&alvos, out->v[i].s[2]);
            if (out->v[i].k == K_GOTO) ot_set_add(&alvos, out->v[i].s[1]);
        }
        OtLista *novo = ot_lista_nova();
        int mudou = 0;
        for (int idx = 0; idx < out->n; idx++) {
            const OtIns *ins = &out->v[idx];
            if (ins->k == K_LABEL && !ot_set_has(&alvos, ins->s[1])) { mudou = 1; continue; }
            if (ins->k == K_GOTO || ins->k == K_IFFALSE) {
                const char *alvo = ins->k == K_GOTO ? ins->s[1] : ins->s[2];
                int j = idx + 1, prox = 0;
                while (j < out->n && out->v[j].k == K_LABEL) {
                    if (strcmp(out->v[j].s[1], alvo) == 0) { prox = 1; break; }
                    j++;
                }
                if (prox) { mudou = 1; continue; }
            }
            ot_lista_add(novo, *ins);
        }
        out = novo;
        if (!mudou) return out;
    }
}

//Code motion de expressoes invariantes
static int ot_div_perigosa(const OtIns *ins) {
    if (ins->k != K_BIN || !(ot_eq(ins->s[3], "/") || ot_eq(ins->s[3], "%"))) return 0;
    oi128 v;
    return !(ot_re_int(ins->s[4]) && ot_int(ins->s[4], &v) && v != 0);
}

//Nenhum salto de fora do laco cai dentro dele
static int ot_rot_pos(const OtLista *instrs, const char *rot) {
    int r = -1;
    for (int i = 0; i < instrs->n; i++)
        if (instrs->v[i].k == K_LABEL && strcmp(instrs->v[i].s[1], rot) == 0) r = i;
    return r;
}

static int ot_entrada_unica(const OtLista *instrs, int j, int i) {
    for (int p = 0; p < instrs->n; p++) {
        const OtIns *ins = &instrs->v[p];
        const char *alvo = ins->k == K_GOTO ? ins->s[1] : ins->k == K_IFFALSE ? ins->s[2] : NULL;
        if (!alvo) continue;
        int r = ot_rot_pos(instrs, alvo);
        if (r >= 0 && j <= r && r <= i && !(j <= p && p <= i)) return 0;
    }
    return 1;
}

typedef struct { int tam, j, i; } OtLaco;

static int ot_cmp_laco(const void *a, const void *b) {
    const OtLaco *x = a, *y = b;
    if (x->tam != y->tam) return x->tam < y->tam ? -1 : 1;
    if (x->j != y->j) return x->j < y->j ? -1 : 1;
    return x->i < y->i ? -1 : x->i > y->i;
}

//Lacos (volta para rotulo anterior) ordenados por tamanho
static OtLaco *ot_lacos(const OtLista *instrs, int *n) {
    OtLaco *l = ot_alloc((size_t)(instrs->n + 1) * sizeof(OtLaco));
    *n = 0;
    for (int i = 0; i < instrs->n; i++) {
        if (instrs->v[i].k != K_GOTO) continue;
        int r = ot_rot_pos(instrs, instrs->v[i].s[1]);
        if (r >= 0 && r < i) { l[*n].tam = i - r; l[*n].j = r; l[*n].i = i; (*n)++; }
    }
    qsort(l, (size_t)*n, sizeof(OtLaco), ot_cmp_laco);
    return l;
}

//Contagem de definicoes por nome
typedef struct { OtMap nomes; int *cont; int cap; } OtContador;

static int ot_cont_get(const OtContador *c, const char *k) {
    int i = ot_map_find(&c->nomes, k);
    return i < 0 ? 0 : c->cont[i];
}

static void ot_cont_inc(OtContador *c, const char *k) {
    int i = ot_map_find(&c->nomes, k);
    if (i < 0) {
        ot_map_put(&c->nomes, k, k);
        i = c->nomes.n - 1;
        if (i >= c->cap) {
            int nc = c->cap ? c->cap * 2 : 16;
            int *nv = ot_alloc((size_t)nc * sizeof(int));
            if (c->cap) memcpy(nv, c->cont, (size_t)c->cap * sizeof(int));
            c->cont = nv; c->cap = nc;
        }
        c->cont[i] = 0;
    }
    c->cont[i]++;
}

static OtLista *ot_mover_invariantes(OtLista *instrs, const OtSet *glob, OtLista *registro) {
    OtContador defs_total = {0};
    for (int i = 0; i < instrs->n; i++) {
        const char *d = ot_defs(&instrs->v[i]);
        if (d) ot_cont_inc(&defs_total, d);
    }
    int nl;
    OtLaco *lacos = ot_lacos(instrs, &nl);
    for (int li = 0; li < nl; li++) {
        int j = lacos[li].j, i = lacos[li].i;
        if (!ot_entrada_unica(instrs, j, i)) continue;
        int tem_call = 0;
        OtContador defs_laco = {0};
        for (int p = j + 1; p < i; p++) {
            if (instrs->v[p].k == K_CALL) tem_call = 1;
            const char *d = ot_defs(&instrs->v[p]);
            if (d) ot_cont_inc(&defs_laco, d);
        }
        char *mover = ot_alloc((size_t)instrs->n + 1);
        memset(mover, 0, (size_t)instrs->n + 1);
        OtSet invar = {0};
        int algum = 0, mudou = 1;
        while (mudou) {
            mudou = 0;
            for (int p = j + 1; p < i; p++) {
                const OtIns *ins = &instrs->v[p];
                if (mover[p] || (ins->k != K_BIN && ins->k != K_UN)) continue;
                const char *d = ins->s[1];
                if (!ot_re_temp(d) || ot_cont_get(&defs_laco, d) != 1 || ot_cont_get(&defs_total, d) != 1
                    || ot_div_perigosa(ins)) continue;
                const char *ops[2];
                int no = ot_usos(ins, ops), ok = 1;
                for (int q = 0; q < no; q++) {
                    int inv = ot_set_has(&invar, ops[q])
                              || (ot_cont_get(&defs_laco, ops[q]) == 0 && !(tem_call && ot_set_has(glob, ops[q])));
                    if (!inv) { ok = 0; break; }
                }
                if (ok) { mover[p] = 1; ot_set_add(&invar, d); mudou = 1; algum = 1; }
            }
        }
        if (algum) {
            OtLista *novo = ot_lista_nova();
            for (int p = 0; p < instrs->n; p++) {
                if (p == j) {
                    for (int q = 0; q < instrs->n; q++) {
                        if (!mover[q]) continue;
                        if (registro) ot_lista_add(registro, instrs->v[q]);
                        ot_lista_add(novo, instrs->v[q]);
                    }
                }
                if (!(p >= j && mover[p])) ot_lista_add(novo, instrs->v[p]);
            }
            return novo;
        }
    }
    return instrs;
}

//Vivacidade e codigo morto
static int ot_removivel(const OtIns *ins) {
    if (ins->k == K_COPY || ins->k == K_UN || ins->k == K_LOAD) return 1;
    if (ins->k == K_BIN) return !ot_div_perigosa(ins);
    return 0;
}

static OtSet *ot_vivas_entrada(const OtCfg *c, const OtLista *instrs, const OtSet *glob) {
    OtSet *vin = ot_alloc((size_t)c->n * sizeof(OtSet));
    memset(vin, 0, (size_t)c->n * sizeof(OtSet));
    for (int it = 0; it < 300; it++) {
        int mudou = 0;
        for (int b = c->n - 1; b >= 0; b--) {
            OtSet live = {0};
            for (int k = 0; k < c->su[b].n; k++) ot_set_union(&live, &vin[c->su[b].s[k]]);
            for (int p = c->bl[b].b - 1; p >= c->bl[b].a; p--) {
                const OtIns *ins = &instrs->v[p];
                const char *d = ot_defs(ins);
                if (d && !ot_set_has(glob, d)) ot_set_del(&live, d);
                const char *ops[2];
                int no = ot_usos(ins, ops);
                for (int q = 0; q < no; q++) ot_set_add(&live, ops[q]);
            }
            if (!ot_set_eq(&live, &vin[b])) { vin[b] = live; mudou = 1; }
        }
        if (!mudou) return vin;
    }
    ot_falha();
    return NULL;
}

static OtLista *ot_eliminar_mortos(OtLista *instrs, const OtSet *glob) {
    OtCfg c = ot_construir_cfg(instrs);
    if (c.n == 0) return instrs;
    OtSet *vin = ot_vivas_entrada(&c, instrs, glob);
    OtLista *out = ot_lista_nova();
    for (int b = 0; b < c.n; b++) {
        OtSet live = {0};
        for (int k = 0; k < c.su[b].n; k++) ot_set_union(&live, &vin[c.su[b].s[k]]);
        int na = c.bl[b].b - c.bl[b].a;
        OtIns *rev = ot_alloc((size_t)(na + 1) * sizeof(OtIns));
        int nr = 0;
        for (int p = c.bl[b].b - 1; p >= c.bl[b].a; p--) {
            OtIns ins = instrs->v[p];
            const char *d = ot_defs(&ins);
            if (d && !ot_set_has(&live, d) && !ot_set_has(glob, d)) {
                if (ot_removivel(&ins)) continue;
                if (ins.k == K_CALL) ins = ot_mk(K_CALL, NULL, ins.s[2], ins.s[3], NULL);
            }
            d = ot_defs(&ins);
            if (d && !ot_set_has(glob, d)) ot_set_del(&live, d);
            const char *ops[2];
            int no = ot_usos(&ins, ops);
            for (int q = 0; q < no; q++) ot_set_add(&live, ops[q]);
            rev[nr++] = ins;
        }
        for (int q = nr - 1; q >= 0; q--) ot_lista_add(out, rev[q]);
    }
    return out;
}

static OtLista *ot_fundir_temporarios(OtLista *instrs, const OtSet *glob) {
    OtCfg c = ot_construir_cfg(instrs);
    if (c.n == 0) return instrs;
    OtSet *vin = ot_vivas_entrada(&c, instrs, glob);
    OtLista *out = ot_lista_nova();
    for (int b = 0; b < c.n; b++) {
        OtSet live = {0};
        for (int k = 0; k < c.su[b].n; k++) ot_set_union(&live, &vin[c.su[b].s[k]]);
        int a = c.bl[b].a, n = c.bl[b].b - c.bl[b].a;
        OtSet *apos = ot_alloc((size_t)(n + 1) * sizeof(OtSet));
        for (int p = n - 1; p >= 0; p--) {
            apos[p] = ot_set_copy(&live);
            const OtIns *ins = &instrs->v[a + p];
            const char *d = ot_defs(ins);
            if (d && !ot_set_has(glob, d)) ot_set_del(&live, d);
            const char *ops[2];
            int no = ot_usos(ins, ops);
            for (int q = 0; q < no; q++) ot_set_add(&live, ops[q]);
        }
        int p = 0;
        while (p < n) {
            const OtIns *ins = &instrs->v[a + p];
            if ((ins->k == K_BIN || ins->k == K_UN || ins->k == K_LOAD || ins->k == K_CALL)
                && ins->s[1] != NULL && ot_re_temp(ins->s[1]) && p + 1 < n
                && instrs->v[a + p + 1].k == K_COPY && strcmp(instrs->v[a + p + 1].s[2], ins->s[1]) == 0
                && !ot_set_has(&apos[p + 1], ins->s[1])) {
                OtIns novo = *ins;
                novo.s[1] = instrs->v[a + p + 1].s[1];
                ot_lista_add(out, novo);
                p += 2;
                continue;
            }
            ot_lista_add(out, *ins);
            p++;
        }
    }
    return out;
}

//Reducao de forca de variavel de inducao
static int ot_fora(const OtLista *instrs, const char *alvo, int j, int i) {
    int r = ot_rot_pos(instrs, alvo);
    return r >= 0 && !(j <= r && r <= i);
}

static OtLista *ot_reduzir_forca(OtLista *instrs, const OtSet *glob) {
    (void)glob;
    OtContador defs_total = {0}, usos_total = {0};
    for (int i = 0; i < instrs->n; i++) {
        const char *d = ot_defs(&instrs->v[i]);
        if (d) ot_cont_inc(&defs_total, d);
        const char *ops[2];
        int no = ot_usos(&instrs->v[i], ops);
        for (int q = 0; q < no; q++) ot_cont_inc(&usos_total, ops[q]);
    }
    int nl;
    OtLaco *lacos = ot_lacos(instrs, &nl);
    for (int li = 0; li < nl; li++) {
        int j = lacos[li].j, i = lacos[li].i;
        if (!ot_entrada_unica(instrs, j, i)) continue;
        for (int q = j + 1; q < i - 1; q++) {
            const OtIns *a = &instrs->v[q], *b = &instrs->v[q + 1];
            if (!(a->k == K_BIN && (ot_eq(a->s[3], "+") || ot_eq(a->s[3], "-")) && ot_re_int(a->s[4])
                  && ot_re_temp(a->s[1]) && strcmp(a->s[2], b->s[1] ? b->s[1] : "") == 0 && b->k == K_COPY
                  && strcmp(b->s[2], a->s[1]) == 0 && !ot_re_temp(b->s[1]))) continue;
            const char *v = b->s[1], *t = a->s[1], *op = a->s[3], *c = a->s[4];
            if (ot_cont_get(&defs_total, t) != 1 || ot_cont_get(&usos_total, t) != 1) continue;
            int cnt = 0;
            for (int x = j + 1; x < i; x++) {
                const char *d = ot_defs(&instrs->v[x]);
                if (d && strcmp(d, v) == 0) cnt++;
            }
            if (cnt != 1) continue;
            int ok = 1;
            for (int x = j + 1; x < q; x++) {
                const OtIns *z = &instrs->v[x];
                if (z->k == K_LABEL) ok = 0;
                if (z->k == K_GOTO && !ot_fora(instrs, z->s[1], j, i)) ok = 0;
                if (z->k == K_IFFALSE && !ot_fora(instrs, z->s[2], j, i)) ok = 0;
            }
            if (!ok) continue;
            int tem_k0 = 0;
            oi128 k0 = 0, cv, passo_v;
            for (int k = j - 1; k >= 0; k--) {
                const OtIns *x = &instrs->v[k];
                if (x->k == K_LABEL || x->k == K_GOTO || x->k == K_IFFALSE || x->k == K_RET) break;
                const char *d = ot_defs(x);
                if (d && strcmp(d, v) == 0) {
                    if (x->k == K_COPY && ot_re_int(x->s[2]) && ot_int(x->s[2], &k0)) tem_k0 = 1;
                    break;
                }
            }
            if (!tem_k0 || !ot_int(c, &cv)) continue;
            passo_v = ot_eq(op, "+") ? cv : -cv;
            for (int p = j + 1; p < q - 1; p++) {
                const OtIns *x = &instrs->v[p], *y = &instrs->v[p + 1];
                if (!(x->k == K_BIN && ot_eq(x->s[3], "*") && ot_re_temp(x->s[1])
                      && y->k == K_COPY && strcmp(y->s[2], x->s[1]) == 0)) continue;
                oi128 m;
                if (strcmp(x->s[2], v) == 0 && ot_re_int(x->s[4])) { if (!ot_int(x->s[4], &m)) continue; }
                else if (strcmp(x->s[4], v) == 0 && ot_re_int(x->s[2])) { if (!ot_int(x->s[2], &m)) continue; }
                else continue;
                const char *w = y->s[1];
                if (ot_re_temp(w) || strcmp(w, v) == 0 || ot_cont_get(&defs_total, x->s[1]) != 1
                    || ot_cont_get(&usos_total, x->s[1]) != 1 || ot_cont_get(&defs_total, w) != 1) continue;
                int fora_do_intervalo = 0;
                for (int n = 0; n < instrs->n; n++)
                    if (ot_usa(&instrs->v[n], w) && !(p + 2 <= n && n <= q - 1)) fora_do_intervalo = 1;
                if (fora_do_intervalo) continue;
                oi128 ini_w = k0 * m, inc_w = passo_v * m;
                if (!(ot_cabe(ini_w) && ot_cabe(inc_w))) continue;
                OtIns atual_w = inc_w >= 0 ? ot_mk(K_BIN, w, w, "+", ot_i2s(inc_w))
                                           : ot_mk(K_BIN, w, w, "-", ot_i2s(-inc_w));
                OtLista *novo = ot_lista_nova();
                for (int n = 0; n < j; n++) ot_lista_add(novo, instrs->v[n]);
                ot_lista_add(novo, ot_mk(K_COPY, w, ot_i2s(ini_w), NULL, NULL));
                for (int n = j; n < p; n++) ot_lista_add(novo, instrs->v[n]);
                for (int n = p + 2; n < q; n++) ot_lista_add(novo, instrs->v[n]);
                ot_lista_add(novo, ot_mk(K_BIN, v, v, op, c));
                ot_lista_add(novo, atual_w);
                for (int n = q + 2; n < instrs->n; n++) ot_lista_add(novo, instrs->v[n]);
                return novo;
            }
        }
    }
    return instrs;
}

//Numeracao dos temporarios sem buracos
static int ot_cmp_i128(const void *a, const void *b) {
    oi128 x = *(const oi128 *)a, y = *(const oi128 *)b;
    return x < y ? -1 : x > y;
}

static int ot_pos_temp(const OtIns *ins, int n) {
    return ins->s[n] != NULL && !(ins->k == K_CALL && n == 2) && ot_re_temp(ins->s[n]);
}

static OtLista *ot_compactar_temps(OtLista *instrs) {
    oi128 *nums = ot_alloc((size_t)(instrs->n * 4 + 1) * sizeof(oi128));
    int nn = 0;
    for (int i = 0; i < instrs->n; i++)
        for (int n = 1; n < 5; n++) {
            oi128 v;
            if (ot_pos_temp(&instrs->v[i], n) && ot_int(instrs->v[i].s[n] + 1, &v)) nums[nn++] = v;
        }
    qsort(nums, (size_t)nn, sizeof(oi128), ot_cmp_i128);
    int u = 0;
    for (int i = 0; i < nn; i++) if (u == 0 || nums[u - 1] != nums[i]) nums[u++] = nums[i];
    int igual = 1;
    for (int r = 0; r < u; r++) if (nums[r] != (oi128)(r + 1)) igual = 0;
    if (igual) return instrs;
    OtLista *out = ot_lista_nova();
    for (int i = 0; i < instrs->n; i++) {
        OtIns ins = instrs->v[i];
        for (int n = 1; n < 5; n++) {
            oi128 v;
            if (!ot_pos_temp(&ins, n) || !ot_int(ins.s[n] + 1, &v)) continue;
            if (strcmp(ot_i2s(v), ins.s[n] + 1) != 0) continue;
            for (int r = 0; r < u; r++)
                if (nums[r] == v) { ins.s[n] = ot_fmt("t%d", r + 1); break; }
        }
        ot_lista_add(out, ins);
    }
    return out;
}

//Alvos
typedef OtLista *(*OtPasso)(OtLista *, const OtSet *);

static OtLista *ot_p_cse(OtLista *i, const OtSet *g) { return ot_cse_local(i, g); }
static OtLista *ot_p_prop_cse(OtLista *i, const OtSet *g) { return ot_propagar(i, g, &OT_CFG_CSE); }
static OtLista *ot_p_prop_alg(OtLista *i, const OtSet *g) { return ot_propagar(i, g, &OT_CFG_ALG); }
static OtLista *ot_p_prop_copia(OtLista *i, const OtSet *g) { return ot_propagar(i, g, &OT_CFG_COPIA); }
static OtLista *ot_p_prop_const(OtLista *i, const OtSet *g) { return ot_propagar(i, g, &OT_CFG_CONST); }
static OtLista *ot_p_fluxo(OtLista *i, const OtSet *g) { (void)g; return ot_limpar_fluxo(i); }
static OtLista *ot_p_mortos(OtLista *i, const OtSet *g) { return ot_eliminar_mortos(i, g); }
static OtLista *ot_p_fundir(OtLista *i, const OtSet *g) { return ot_fundir_temporarios(i, g); }
static OtLista *ot_p_ident(OtLista *i, const OtSet *g) { (void)g; return i; }

static OtLista *ot_estavel(OtLista *instrs, const OtSet *glob, const OtPasso *passes, int np) {
    for (int it = 0; it < 30; it++) {
        OtLista *antes = instrs;
        for (int k = 0; k < np; k++) instrs = passes[k](instrs, glob);
        if (ot_lista_eq(instrs, antes)) break;
    }
    return instrs;
}

static int ot_tem_inalcancavel(const OtLista *instrs) {
    int morto = 0;
    for (int i = 0; i < instrs->n; i++) {
        int k = instrs->v[i].k;
        if (k == K_LABEL) morto = 0;
        else if (morto) return 1;
        if (k == K_RET || k == K_GOTO) morto = 1;
    }
    return 0;
}

static int ot_tem_algebra(const OtLista *instrs) {
    for (int i = 0; i < instrs->n; i++) {
        const OtIns *x = &instrs->v[i];
        if (x->k == K_BIN && !(ot_eh_lit(x->s[2]) && ot_eh_lit(x->s[4]))
            && ot_alg_bin(x->s[3], x->s[2], x->s[4]) != NULL) return 1;
    }
    return 0;
}

static int ot_tem_copia_de_usuario(const OtLista *instrs) {
    for (int i = 0; i < instrs->n; i++) {
        const OtIns *x = &instrs->v[i];
        if (x->k == K_COPY && !ot_eh_lit(x->s[2]) && !ot_re_temp(x->s[2]) && !ot_re_temp(x->s[1])) return 1;
    }
    return 0;
}

static OtLista *ot_loop_licm(OtLista *instrs, const OtSet *glob, OtLista *registro) {
    for (int it = 0; it < 30; it++) {
        OtLista *novo = ot_mover_invariantes(instrs, glob, registro);
        if (ot_lista_eq(novo, instrs)) break;
        instrs = novo;
    }
    return instrs;
}

static OtLista *ot_loop_sr(OtLista *instrs, const OtSet *glob) {
    for (int it = 0; it < 30; it++) {
        OtLista *novo = ot_reduzir_forca(instrs, glob);
        if (ot_lista_eq(novo, instrs)) break;
        instrs = novo;
    }
    return instrs;
}

//Pipeline completo de uma funcao
static OtLista *ot_otimizar_funcao(OtLista *instrs, const OtSet *glob) {
    for (int it = 0; it < 30; it++) {
        OtLista *antes = instrs;
        instrs = ot_propagar(instrs, glob, &OT_CFG_TOTAL);
        instrs = ot_cse_local(instrs, glob);
        instrs = ot_limpar_fluxo(instrs);
        instrs = ot_mover_invariantes(instrs, glob, NULL);
        instrs = ot_eliminar_mortos(instrs, glob);
        if (ot_lista_eq(instrs, antes)) break;
    }
    instrs = ot_fundir_temporarios(instrs, glob);
    return ot_limpar_fluxo(instrs);
}

//Lista de registro das instrucoes movidas pelo alvo licm
static OtLista *ot_registro_licm;

static int ot_d_inalc(OtLista *f, const OtSet *g) { (void)g; return ot_tem_inalcancavel(f); }
static int ot_d_licm(OtLista *f, const OtSet *g) { return !ot_lista_eq(ot_loop_licm(f, g, NULL), f); }
static int ot_d_forca(OtLista *f, const OtSet *g) { return !ot_lista_eq(ot_reduzir_forca(f, g), f); }
static int ot_d_cse(OtLista *f, const OtSet *g) { return !ot_lista_eq(ot_cse_local(f, g), f); }
static int ot_d_alg(OtLista *f, const OtSet *g) { (void)g; return ot_tem_algebra(f); }
static int ot_d_copias(OtLista *f, const OtSet *g) { (void)g; return ot_tem_copia_de_usuario(f); }
static int ot_d_morta(OtLista *f, const OtSet *g) { return !ot_lista_eq(ot_eliminar_mortos(f, g), f); }
static int ot_d_const(OtLista *f, const OtSet *g) { return !ot_lista_eq(ot_propagar(f, g, &OT_CFG_CONST), f); }
static int ot_d_geral(OtLista *f, const OtSet *g) { return !ot_lista_eq(ot_otimizar_funcao(f, g), f); }

static OtLista *ot_a_inalc(OtLista *f, const OtSet *g) { (void)g; return ot_compactar_temps(ot_limpar_fluxo(f)); }
static OtLista *ot_a_licm(OtLista *f, const OtSet *g) { return ot_compactar_temps(ot_loop_licm(f, g, ot_registro_licm)); }
static OtLista *ot_a_forca(OtLista *f, const OtSet *g) {
    return ot_compactar_temps(ot_eliminar_mortos(ot_loop_sr(f, g), g));
}
static OtLista *ot_a_cse(OtLista *f, const OtSet *g) {
    const OtPasso ps[] = { ot_p_cse, ot_p_prop_cse, ot_p_mortos };
    return ot_compactar_temps(ot_estavel(f, g, ps, 3));
}
static OtLista *ot_a_alg(OtLista *f, const OtSet *g) {
    const OtPasso ps[] = { ot_p_prop_alg, ot_p_mortos };
    return ot_compactar_temps(ot_estavel(f, g, ps, 2));
}
static OtLista *ot_a_copias(OtLista *f, const OtSet *g) {
    const OtPasso ps[] = { ot_p_prop_copia, ot_p_mortos };
    return ot_compactar_temps(ot_estavel(f, g, ps, 2));
}
static OtLista *ot_a_morta(OtLista *f, const OtSet *g) { return ot_compactar_temps(ot_eliminar_mortos(f, g)); }
static OtLista *ot_a_const(OtLista *f, const OtSet *g) {
    const OtPasso ps[] = { ot_p_prop_const, ot_p_fluxo, ot_p_mortos };
    return ot_compactar_temps(ot_estavel(f, g, ps, 3));
}
static OtLista *ot_a_geral(OtLista *f, const OtSet *g) { return ot_compactar_temps(ot_otimizar_funcao(f, g)); }

typedef struct { const char *nome; int (*detecta)(OtLista *, const OtSet *); OtPasso aplica; } OtAlvo;

//A ordem e a prioridade
static const OtAlvo OT_ALVOS[] = {
    { "inalcancavel", ot_d_inalc, ot_a_inalc },
    { "licm", ot_d_licm, ot_a_licm },
    { "forca", ot_d_forca, ot_a_forca },
    { "cse", ot_d_cse, ot_a_cse },
    { "algebra", ot_d_alg, ot_a_alg },
    { "copias", ot_d_copias, ot_a_copias },
    { "morta", ot_d_morta, ot_a_morta },
    { "constantes", ot_d_const, ot_a_const },
    { "geral", ot_d_geral, ot_a_geral },
};
#define OT_NALVOS ((int)(sizeof OT_ALVOS / sizeof OT_ALVOS[0]))

//Partes do programa: funcoes ou linhas soltas
typedef struct { const char **v; int n, cap; } OtLinhas;

typedef struct {
    int func;
    const char *linha;
    const char *nome;
    const char **params; int nparams;
    OtLista *corpo;
    OtLinhas raw;
} OtParte;

static void ot_linhas_add(OtLinhas *l, const char *s) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        const char **nv = ot_alloc((size_t)l->cap * sizeof(char *));
        if (l->n) memcpy(nv, l->v, (size_t)l->n * sizeof(char *));
        l->v = nv;
    }
    l->v[l->n++] = s;
}

//Casa "func NOME" ou "func NOME(PARAMS)"
static int ot_func_nome(const char *linha, const char **nome, const char **params) {
    const char *rest = linha + 5;
    size_t n = strlen(rest);
    *params = NULL;
    for (size_t l = 1; l <= n; l++) {
        if (ot_isspace((unsigned char)rest[l - 1])) return 0;
        const char *r = rest + l;
        size_t rn = n - l;
        if (rn == 0) { *nome = ot_dup_n(rest, l); return 1; }
        if (rn >= 2 && r[0] == '(' && r[rn - 1] == ')') {
            *nome = ot_dup_n(rest, l);
            *params = ot_dup_n(r + 1, rn - 2);
            return 1;
        }
    }
    return 0;
}

static int ot_nome_glob(const char *linha, const char **nome) {
    const char *p = linha;
    while (*p && ot_isspace((unsigned char)*p)) p++;
    size_t tk = ot_tok(p);
    if (tk > 0 && ot_startswith(p + tk, " = ")) { *nome = ot_dup_n(p, tk); return 1; }
    return 0;
}

//Quebra o texto em partes e descobre os nomes globais
static OtParte *ot_segmentar(const char *texto, int *np, OtSet *glob) {
    size_t len = strlen(texto);
    while (len > 0 && texto[len - 1] == '\n') len--;
    int nl = 0;
    const char **linhas = ot_alloc((len + 2) * sizeof(char *));
    size_t ini = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || texto[i] == '\n') {
            linhas[nl++] = ot_dup_n(texto + ini, i - ini);
            ini = i + 1;
        }
    }
    OtParte *partes = ot_alloc((size_t)(nl + 1) * sizeof(OtParte));
    memset(partes, 0, (size_t)(nl + 1) * sizeof(OtParte));
    *np = 0;
    int i = 0;
    while (i < nl) {
        OtParte *p = &partes[(*np)++];
        if (ot_startswith(linhas[i], "func ")) {
            int j = i + 1;
            while (j < nl && strcmp(linhas[j], "endfunc") != 0) j++;
            if (j >= nl) ot_falha();
            p->func = 1;
            p->linha = linhas[i];
            const char *pr = NULL;
            if (!ot_func_nome(linhas[i], &p->nome, &pr)) p->nome = "";
            p->nparams = 0;
            if (pr) {
                size_t pl = strlen(pr);
                p->params = ot_alloc((pl + 2) * sizeof(char *));
                const char *ini2 = pr;
                for (const char *q = pr;; q++) {
                    if (*q == ',' || *q == '\0') {
                        const char *s = ot_strip(ot_dup_n(ini2, (size_t)(q - ini2)));
                        if (*s) p->params[p->nparams++] = s;
                        ini2 = q + 1;
                        if (*q == '\0') break;
                    }
                }
            }
            OtLista *corpo = ot_lista_nova();
            int ok = 1;
            for (int k = i + 1; k < j; k++) {
                ot_linhas_add(&p->raw, linhas[k]);
                OtIns ins;
                if (ok && ot_parse(linhas[k], &ins)) ot_lista_add(corpo, ins);
                else ok = 0;
            }
            p->corpo = ok ? corpo : NULL;
            i = j + 1;
        } else {
            p->func = 0;
            p->linha = linhas[i];
            i++;
        }
    }
    memset(glob, 0, sizeof *glob);
    for (int k = 0; k < *np; k++) {
        const char *nm;
        if (!partes[k].func && ot_nome_glob(partes[k].linha, &nm)) ot_set_add(glob, nm);
    }
    OtMap cont = {0};
    int cap = 0;
    int *cn = NULL;
    for (int k = 0; k < *np; k++) {
        if (!partes[k].func || !partes[k].corpo) continue;
        OtSet nomes = {0};
        for (int q = 0; q < partes[k].corpo->n; q++) {
            const OtIns *ins = &partes[k].corpo->v[q];
            const char *d = ot_defs(ins);
            if (d && *d) ot_set_add(&nomes, d);
            const char *ops[2];
            int no = ot_usos(ins, ops);
            for (int z = 0; z < no; z++) ot_set_add(&nomes, ops[z]);
        }
        for (int z = 0; z < nomes.n; z++) {
            const char *x = nomes.v[z];
            if (ot_re_temp(x)) continue;
            int eh_param = 0;
            for (int w = 0; w < partes[k].nparams; w++) if (strcmp(partes[k].params[w], x) == 0) eh_param = 1;
            if (eh_param) continue;
            int idx = ot_map_find(&cont, x);
            if (idx < 0) {
                ot_map_put(&cont, x, x);
                idx = cont.n - 1;
                if (idx >= cap) {
                    int nc = cap ? cap * 2 : 16;
                    int *nv = ot_alloc((size_t)nc * sizeof(int));
                    if (cap) memcpy(nv, cn, (size_t)cap * sizeof(int));
                    cn = nv; cap = nc;
                }
                cn[idx] = 0;
            }
            cn[idx]++;
        }
    }
    for (int z = 0; z < cont.n; z++) if (cn[z] > 1) ot_set_add(glob, cont.k[z]);
    return partes;
}

//Texto do programa com os corpos dados
static const char *ot_montar(OtParte **ps, int np, const OtLinhas *corpos) {
    OtLinhas saida = {0};
    int k = 0;
    for (int i = 0; i < np; i++) {
        if (!ps[i]->func) { ot_linhas_add(&saida, ps[i]->linha); continue; }
        ot_linhas_add(&saida, ps[i]->linha);
        for (int z = 0; z < corpos[k].n; z++) ot_linhas_add(&saida, corpos[k].v[z]);
        ot_linhas_add(&saida, "endfunc");
        k++;
    }
    size_t tot = 2;
    for (int i = 0; i < saida.n; i++) tot += strlen(saida.v[i]) + 1;
    char *r = ot_alloc(tot), *w = r;
    for (int i = 0; i < saida.n; i++) { size_t l = strlen(saida.v[i]); memcpy(w, saida.v[i], l); w += l; *w++ = '\n'; }
    *w = '\0';
    return r;
}

static const char *ot_juntar_linhas(const OtLinhas *l, const char *sep) {
    size_t tot = 1, ls = strlen(sep);
    for (int i = 0; i < l->n; i++) tot += strlen(l->v[i]) + ls;
    char *r = ot_alloc(tot), *w = r;
    for (int i = 0; i < l->n; i++) {
        if (i) { memcpy(w, sep, ls); w += ls; }
        size_t n = strlen(l->v[i]);
        memcpy(w, l->v[i], n);
        w += n;
    }
    *w = '\0';
    return r;
}

static const char *ot_rstrip_nl(const char *s) {
    size_t n = strlen(s);
    while (n > 0 && s[n - 1] == '\n') n--;
    return ot_dup_n(s, n);
}

//Aplica o passe protegido contra falha; devolve 0 se falhou
static int ot_tentar(OtPasso fn, OtLista *in, const OtSet *glob, OtLista **out) {
    jmp_buf jb;
    jmp_buf *ant = ot_handler;
    ot_handler = &jb;
    if (setjmp(jb) == 0) {
        *out = fn(in, glob);
        ot_handler = ant;
        return 1;
    }
    ot_handler = ant;
    return 0;
}

//Corpo formatado de cada funcao; mantem o original se o passe falha
static OtLinhas *ot_aplicar(OtParte *partes, int np, const OtSet *glob, OtPasso fn) {
    OtLinhas *corpos = ot_alloc((size_t)(np + 1) * sizeof(OtLinhas));
    int k = 0;
    for (int i = 0; i < np; i++) {
        if (!partes[i].func) continue;
        OtLinhas *c = &corpos[k++];
        memset(c, 0, sizeof *c);
        OtLista *r = NULL;
        if (partes[i].corpo == NULL || !ot_tentar(fn, partes[i].corpo, glob, &r)) {
            for (int z = 0; z < partes[i].raw.n; z++) ot_linhas_add(c, partes[i].raw.v[z]);
            continue;
        }
        for (int z = 0; z < r->n; z++) ot_linhas_add(c, ot_fmt_ins(&r->v[z]));
    }
    return corpos;
}

//Todas as otimizacoes juntas; devolve texto alocado com malloc
static char *ot_otimizar(const char *ir_texto) {
    int np;
    OtSet glob;
    OtParte *partes = ot_segmentar(ir_texto, &np, &glob);
    OtLinhas *corpos = ot_aplicar(partes, np, &glob, ot_otimizar_funcao);
    OtParte **ps = ot_alloc((size_t)(np + 1) * sizeof(OtParte *));
    for (int i = 0; i < np; i++) ps[i] = &partes[i];
    char *r = strdup(ot_montar(ps, np, corpos));
    ot_liberar();
    return r;
}

//Relatorio por alvo
typedef struct {
    char *alvo, *classe, *titulo, *ref, *opt;
    int aviso;
    char *extras[4];
    int nextras;
} OtRelatorio;

static const char *ot_juntar_nomes(const char **n, int cnt) {
    if (cnt == 1) return n[0];
    OtLinhas l = {0};
    for (int i = 0; i < cnt - 1; i++) ot_linhas_add(&l, n[i]);
    return ot_fmt("%s e %s", ot_juntar_linhas(&l, ", "), n[cnt - 1]);
}

//Corpo da main ou programa inteiro
static const char *ot_corpo_exibicao(OtParte *partes, int np, const OtLinhas *corpos, int referencia) {
    int nf = 0, ult = -1;
    for (int i = 0; i < np; i++) if (partes[i].func) { nf++; if (ult < 0) ult = i; }
    if (nf == 1 && strcmp(partes[ult].nome, "main") == 0 && np == 1) {
        OtLinhas l = corpos[0];
        if (referencia && l.n > 0 && strcmp(l.v[l.n - 1], "  return 0") == 0) l.n--;
        return ot_juntar_linhas(&l, "\n");
    }
    OtParte **ps = ot_alloc((size_t)(np + 1) * sizeof(OtParte *));
    for (int i = 0; i < np; i++) ps[i] = &partes[i];
    return ot_rstrip_nl(ot_montar(ps, np, corpos));
}

//Palavras do texto que sao nomes visiveis (sem temporarios)
static int ot_palavra(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80;
}

static OtSet ot_visiveis(const char *t) {
    OtSet s = {0};
    size_t n = strlen(t);
    for (size_t i = 0; i < n;) {
        unsigned char c = (unsigned char)t[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')) { i++; continue; }
        size_t j = i + 1;
        while (j < n && ot_palavra((unsigned char)t[j])) j++;
        if (j + 1 < n && t[j] == '#' && ot_palavra((unsigned char)t[j + 1])) {
            j++;
            while (j < n && ot_palavra((unsigned char)t[j])) j++;
        }
        const char *w = ot_dup_n(t + i, j - i);
        if (!ot_re_temp(w)) ot_set_add(&s, w);
        i = j;
    }
    return s;
}

static int ot_cmp_str(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

//Detecta chamada sem argumentos
static int ot_re_call_zero(const char *l) {
    const char *p = l;
    while (*p && ot_isspace((unsigned char)*p)) p++;
    const char *rest = p;
    size_t tk = ot_tok(p);
    if (tk > 0 && ot_startswith(p + tk, " = ")) rest = p + tk + 3;
    for (int tentativa = 0; tentativa < 2; tentativa++) {
        const char *r = tentativa == 0 ? rest : p;
        if (ot_startswith(r, "call ")) {
            const char *x = r + 5;
            size_t n = strlen(x);
            if (n >= 4 && strcmp(x + n - 3, ", 0") == 0 && ot_todo_sem_espaco(x, n - 3)) return 1;
        }
        if (rest == p) break;
    }
    return 0;
}

static int ot_trim_startswith(const char *l, const char *p) {
    return ot_startswith(ot_strip(l), p);
}

static void ot_extra(OtRelatorio *r, const char *s) { r->extras[r->nextras++] = strdup(s); }

//Escolhe o alvo, aplica so o passe dele e monta o relatorio
static OtRelatorio ot_otimizar_alvo(const char *ir_texto) {
    OtRelatorio r;
    memset(&r, 0, sizeof r);
    int np;
    OtSet glob;
    OtParte *partes = ot_segmentar(ir_texto, &np, &glob);
    int *funcs = ot_alloc((size_t)(np + 1) * sizeof(int));
    int nf = 0;
    for (int i = 0; i < np; i++) if (partes[i].func && partes[i].corpo) funcs[nf++] = i;
    int escolhido = -1;
    for (int a = 0; a < OT_NALVOS && escolhido < 0; a++) {
        jmp_buf jb;
        jmp_buf *ant = ot_handler;
        ot_handler = &jb;
        volatile int achou = 0;
        if (setjmp(jb) == 0) {
            for (int f = 0; f < nf && !achou; f++)
                if (OT_ALVOS[a].detecta(partes[funcs[f]].corpo, &glob)) achou = 1;
        }
        ot_handler = ant;
        if (achou) escolhido = a;
    }
    OtLinhas *ref = ot_aplicar(partes, np, &glob, ot_p_ident);
    const char *titulo = "IR otimizada esperada (-O1), equivalente:";
    if (escolhido < 0) {
        r.alvo = strdup("nenhuma oportunidade de otimização encontrada");
        r.classe = strdup("nenhuma");
        r.ref = strdup(ot_corpo_exibicao(partes, np, ref, 1));
        r.opt = strdup(ot_corpo_exibicao(partes, np, ref, 0));
        r.titulo = strdup(titulo);
        ot_liberar();
        return r;
    }
    const char *classe = OT_ALVOS[escolhido].nome;
    OtLista *registro = ot_lista_nova();
    ot_registro_licm = registro;
    OtLinhas *corpos = ot_aplicar(partes, np, &glob, OT_ALVOS[escolhido].aplica);
    const char *alvo;
    int aviso = 0;
    OtLinhas orig = {0}, opt = {0};
    for (int i = 0; i < np; i++)
        if (partes[i].func) for (int z = 0; z < partes[i].raw.n; z++) ot_linhas_add(&orig, partes[i].raw.v[z]);
    int kf = 0;
    for (int i = 0; i < np; i++)
        if (partes[i].func) { for (int z = 0; z < corpos[kf].n; z++) ot_linhas_add(&opt, corpos[kf].v[z]); kf++; }
    const char *orig_txt = ot_juntar_linhas(&orig, "\n"), *opt_txt = ot_juntar_linhas(&opt, "\n");
    if (strcmp(classe, "inalcancavel") == 0) {
        alvo = "eliminação de código morto/inalcançável";
        aviso = 1;
        titulo = "IR otimizada esperada (-O1):";
    } else if (strcmp(classe, "licm") == 0) {
        alvo = "code motion de expressão loop-invariant";
        const char *ops[256];
        int no = 0;
        for (int i = 0; i < registro->n; i++) {
            const char *us[2];
            int nu = ot_usos(&registro->v[i], us), antes = no;
            for (int z = 0; z < nu; z++) {
                int ja = 0;
                for (int w = 0; w < antes; w++) if (strcmp(ops[w], us[z]) == 0) ja = 1;
                if (!ja && no < 256) ops[no++] = us[z];
            }
        }
        if (no) {
            const char *verbo = no > 1 ? "são modificados" : "é modificado";
            ot_extra(&r, ot_fmt("Observação: mover antes do laço é seguro porque %s não %s no laço.",
                                ot_juntar_nomes(ops, no), verbo));
        }
    } else if (strcmp(classe, "forca") == 0) {
        alvo = "redução de força / atualização incremental de expressão relacionada à indução";
        titulo = "IR otimizada possível (-O1), equivalente:";
        ot_extra(&r, "Critério: validar equivalência para cada iteração; diferentes implementações podem "
                     "preferir não aplicar esta otimização. Não exigir forma textual idêntica.");
    } else if (strcmp(classe, "cse") == 0) {
        alvo = "eliminação de subexpressão comum local";
    } else if (strcmp(classe, "algebra") == 0) {
        alvo = "simplificação algébrica e propagação de cópias";
    } else if (strcmp(classe, "copias") == 0) {
        alvo = "eliminação de cópias";
    } else if (strcmp(classe, "morta") == 0) {
        alvo = "eliminação de atribuição morta";
        OtSet vo = ot_visiveis(orig_txt), vp = ot_visiveis(opt_txt);
        static const char *const excl[] = { "func", "print", "return", "goto", "ifFalse", "param", "call",
            "read", "load", "store", "not", "and", "or", "true", "false", "endfunc" };
        const char **sum = ot_alloc((size_t)(vo.n + 1) * sizeof(char *));
        int ns = 0;
        for (int i = 0; i < vo.n; i++) {
            const char *v = vo.v[i];
            if (ot_set_has(&vp, v) || ot_startswith(v, "L_")) continue;
            int ex = 0;
            for (size_t z = 0; z < sizeof excl / sizeof excl[0]; z++) if (strcmp(v, excl[z]) == 0) ex = 1;
            if (!ex) sum[ns++] = v;
        }
        qsort(sum, (size_t)ns, sizeof(char *), ot_cmp_str);
        if (ns) ot_extra(&r, ot_fmt("Verificar: cálculo/definição de %s não tem uso observável e pode ser removido.",
                                    ot_juntar_nomes(sum, ns)));
    } else if (strcmp(classe, "constantes") == 0) {
        int antes_if = 0, depois_if = 0, literal_literal = 0;
        for (int f = 0; f < nf; f++)
            for (int q = 0; q < partes[funcs[f]].corpo->n; q++) {
                const OtIns *i = &partes[funcs[f]].corpo->v[q];
                if (i->k == K_IFFALSE) antes_if++;
                if (i->k == K_BIN && ot_eh_lit(i->s[2]) && ot_eh_lit(i->s[4])) literal_literal = 1;
            }
        for (int z = 0; z < opt.n; z++) if (ot_trim_startswith(opt.v[z], "ifFalse")) depois_if++;
        if (depois_if < antes_if) {
            alvo = "dobra de condição constante + eliminação de código inalcançável";
        } else if (literal_literal) {
            alvo = "propagação de constantes + simplificação/dobra algébrica";
            const char *unico = NULL;
            int np2 = 0;
            for (int z = 0; z < opt.n; z++) {
                const char *s = ot_strip(opt.v[z]);
                if (ot_startswith(s, "print ")) { unico = s + 6; np2++; }
            }
            if (np2 == 1 && ot_eh_lit(unico))
                ot_extra(&r, ot_fmt("Verificar: remoção de temporários/atribuições tornou-se possível; "
                                    "resultado observável permanece %s.", unico));
        } else {
            alvo = "reaching definitions/propagação global de constantes e dobra";
        }
    } else {
        alvo = "otimizações gerais (constantes, cópias, subexpressões, código morto)";
    }
    //Apresentacao da referencia: so muda o que e exibido
    const char *ref_txt;
    if (strcmp(classe, "inalcancavel") == 0) {
        OtParte **sel = ot_alloc((size_t)(np + 1) * sizeof(OtParte *));
        OtLinhas *selc = ot_alloc((size_t)(np + 1) * sizeof(OtLinhas));
        int ns = 0, k2 = 0;
        for (int i = 0; i < np; i++) {
            if (!partes[i].func) continue;
            if (partes[i].corpo && ot_tem_inalcancavel(partes[i].corpo)) { sel[ns] = &partes[i]; selc[ns] = ref[k2]; ns++; }
            k2++;
        }
        ref_txt = ot_rstrip_nl(ot_montar(sel, ns, selc));
    } else {
        int tem_verificar = 0;
        for (int e = 0; e < r.nextras; e++) if (ot_startswith(r.extras[e], "Verificar")) tem_verificar = 1;
        if (tem_verificar) ref = ot_aplicar(partes, np, &glob, ot_p_fundir);
        ref_txt = ot_corpo_exibicao(partes, np, ref, 1);
        if (strcmp(classe, "licm") == 0 || strcmp(classe, "forca") == 0) {
            OtLinhas ls = {0};
            const char *q = ref_txt;
            for (;;) {
                const char *nl = strchr(q, '\n');
                ot_linhas_add(&ls, nl ? ot_dup_n(q, (size_t)(nl - q)) : ot_dup(q));
                if (!nl) break;
                q = nl + 1;
            }
            int volta = -1;
            for (int n = 0; n < ls.n; n++) {
                const char *st = ot_strip(ls.v[n]);
                if (!ot_startswith(st, "goto ")) continue;
                const char *alvo_l = ot_fmt("%s:", st + 5);
                int pos = n + 1;
                for (int z = 0; z < ls.n; z++)
                    if (ot_endswith(ls.v[z], ":") && strcmp(ot_strip(ls.v[z]), alvo_l) == 0) pos = z;
                if (pos < n) volta = n;
            }
            if (volta >= 0) { ls.n = volta + 1; ref_txt = ot_juntar_linhas(&ls, "\n"); }
        }
    }
    const char *opt_exib = ot_corpo_exibicao(partes, np, corpos, 0);
    if (strcmp(classe, "inalcancavel") == 0) {
        //Ajuste de exibicao para chamada sem argumentos
        OtLinhas ls = {0}, saida = {0};
        const char *q = opt_exib;
        for (;;) {
            const char *nl = strchr(q, '\n');
            ot_linhas_add(&ls, nl ? ot_dup_n(q, (size_t)(nl - q)) : ot_dup(q));
            if (!nl) break;
            q = nl + 1;
        }
        for (int n = 0; n < ls.n; n++) {
            if (ot_re_call_zero(ls.v[n])) ot_linhas_add(&saida, "  param");
            ot_linhas_add(&saida, ls.v[n]);
        }
        opt_exib = ot_juntar_linhas(&saida, "\n");
    }
    r.alvo = strdup(alvo);
    r.classe = strdup(classe);
    r.aviso = aviso;
    r.titulo = strdup(titulo);
    r.ref = strdup(ref_txt);
    r.opt = strdup(opt_exib);
    ot_liberar();
    return r;
}

static void ot_relatorio_liberar(OtRelatorio *r) {
    free(r->alvo); free(r->classe); free(r->titulo); free(r->ref); free(r->opt);
    for (int i = 0; i < r->nextras; i++) free(r->extras[i]);
}
