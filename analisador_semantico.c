//Analisador semantico do MiniC
#if defined(__APPLE__)
#  define MINIC_ASM(n) "_" n
#else
#  define MINIC_ASM(n) n
#endif
int main(int argc, char *argv[]) __asm__(MINIC_ASM("minic_parser_main_nao_usado"));
#include "parser.c"

//Valores dinamicos da AST
typedef enum { PV_NONE, PV_BOOL, PV_INT, PV_FLOAT, PV_STR, PV_LIST, PV_TUPLE, PV_DICT, PV_SYM } PVKind;
typedef struct Simbolo Simbolo;
typedef struct PV PV;
struct PV {
    PVKind   k;
    int      b;
    double   f;
    char    *s;
    size_t   sn;
    PV     **v;
    char   **keys;
    int      n, cap;
    Simbolo *sym;
};

static PV PV_NONE_OBJ = { PV_NONE, 0, 0.0, NULL, 0, NULL, NULL, 0, 0, NULL };
#define PVNONE (&PV_NONE_OBJ)

static PV *pv_new(PVKind k) {
    PV *p = xmalloc(sizeof(PV));
    memset(p, 0, sizeof(PV));
    p->k = k;
    return p;
}
static PV *pv_bool(int b)            { PV *p = pv_new(PV_BOOL);  p->b = b != 0; return p; }
static PV *pv_float(double f)        { PV *p = pv_new(PV_FLOAT); p->f = f; return p; }
static PV *pv_str(const char *s, size_t n) { PV *p = pv_new(PV_STR); p->s = xmemdup(s, n); p->sn = n; return p; }
static PV *pv_cstr(const char *s)    { return s ? pv_str(s, strlen(s)) : PVNONE; }
static PV *pv_int_s(const char *d)   { PV *p = pv_new(PV_INT); p->s = xstrdup(d); p->sn = strlen(d); return p; }
static PV *pv_int(int v)             { char b[32]; snprintf(b, sizeof b, "%d", v); return pv_int_s(b); }
static PV *pv_list(void)             { return pv_new(PV_LIST); }
static PV *pv_dict(void)             { return pv_new(PV_DICT); }
static PV *pv_sym(Simbolo *s)        { PV *p = pv_new(PV_SYM); p->sym = s; return p; }

static void pv_push(PV *l, PV *x) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 4;
        l->v = xrealloc(l->v, (size_t)l->cap * sizeof(PV *));
    }
    l->v[l->n++] = x;
}
static PV *pv_get(PV *d, const char *k) {
    for (int i = 0; i < d->n; i++) if (strcmp(d->keys[i], k) == 0) return d->v[i];
    return NULL;
}
static void pv_set(PV *d, const char *k, PV *x) {
    for (int i = 0; i < d->n; i++) if (strcmp(d->keys[i], k) == 0) { d->v[i] = x; return; }
    if (d->n == d->cap) {
        d->cap = d->cap ? d->cap * 2 : 4;
        d->v = xrealloc(d->v, (size_t)d->cap * sizeof(PV *));
        d->keys = xrealloc(d->keys, (size_t)d->cap * sizeof(char *));
    }
    d->keys[d->n] = xstrdup(k);
    d->v[d->n++] = x;
}

//Strings com tamanho
typedef struct { char *p; size_t n; } Str;

typedef struct { int ok; int neg; char *d; } BI;

static Str str_from_sb(StrBuf *sb) {
    Str r;
    if (!sb->dados) { r.p = xmemdup("", 0); r.n = 0; }
    else { r.p = sb->dados; r.n = sb->tamanho; }
    return r;
}

static char *bi_str(BI b) {
    size_t n = strlen(b.d);
    char *r = xmalloc(n + 2);
    size_t o = 0;
    if (b.neg) r[o++] = '-';
    memcpy(r + o, b.d, n + 1);
    return r;
}

static Str mk(const char *fmt, ...) {
    StrBuf sb = {0};
    va_list ap;
    va_start(ap, fmt);
    for (const char *c = fmt; *c; c++) {
        if (*c != '%') { sb_addc(&sb, *c); continue; }
        c++;
        switch (*c) {
            case 's': { const char *s = va_arg(ap, const char *); sb_add(&sb, s ? s : "None"); break; }
            case 'd': { int v = va_arg(ap, int); sb_add_int(&sb, v); break; }
            case 'S': { Str s = va_arg(ap, Str); sb_add_n(&sb, s.p, s.n); break; }
            case 'B': { BI b = va_arg(ap, BI); char *t = bi_str(b); sb_add(&sb, t); free(t); break; }
            case '%': sb_addc(&sb, '%'); break;
            default:  sb_addc(&sb, '%'); sb_addc(&sb, *c); break;
        }
    }
    va_end(ap);
    return str_from_sb(&sb);
}

//Utilitarios de texto
static unsigned cp_decode(const char *s, size_t i, size_t n, int *len) {
    unsigned char c = (unsigned char)s[i];
    int L = 1; unsigned cp = c;
    if (c >= 0xF0)      { L = 4; cp = c & 0x07; }
    else if (c >= 0xE0) { L = 3; cp = c & 0x0F; }
    else if (c >= 0xC0) { L = 2; cp = c & 0x1F; }
    if (i + (size_t)L > n) L = (int)(n - i);
    for (int k = 1; k < L; k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
    *len = L;
    return cp;
}

static int imprimivel_cp(unsigned cp) {
    if (cp <= 0xA0 || cp == 0xAD) return 0;
    if (cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200F)) return 0;
    if (cp >= 0x2028 && cp <= 0x202F) return 0;
    if (cp >= 0x205F && cp <= 0x206F) return 0;
    if (cp == 0x3000 || cp == 0xFEFF || (cp >= 0xFFF9 && cp <= 0xFFFB)) return 0;
    if (cp >= 0xD800 && cp <= 0xF8FF) return 0;
    if ((cp & 0xFFFE) == 0xFFFE || (cp >= 0xFDD0 && cp <= 0xFDEF)) return 0;
    if (cp >= 0xE0000) return 0;
    return 1;
}

static void repr_str(StrBuf *sb, const char *s, size_t n) {
    int sq = 0, dq = 0;
    for (size_t i = 0; i < n; i++) { if (s[i] == '\'') sq = 1; if (s[i] == '"') dq = 1; }
    char q = (sq && !dq) ? '"' : '\'';
    sb_addc(sb, q);
    size_t i = 0;
    while (i < n) {
        int L; unsigned cp = cp_decode(s, i, n, &L);
        char tmp[16];
        if (cp < 0x80) {
            char c = (char)cp;
            if (c == q || c == '\\') { sb_addc(sb, '\\'); sb_addc(sb, c); }
            else if (c == '\t') sb_add(sb, "\\t");
            else if (c == '\n') sb_add(sb, "\\n");
            else if (c == '\r') sb_add(sb, "\\r");
            else if (cp < 0x20 || cp == 0x7f) { snprintf(tmp, sizeof tmp, "\\x%02x", cp); sb_add(sb, tmp); }
            else sb_addc(sb, c);
        } else if (imprimivel_cp(cp)) {
            sb_add_n(sb, s + i, (size_t)L);
        } else if (cp < 0x100)   { snprintf(tmp, sizeof tmp, "\\x%02x", cp); sb_add(sb, tmp); }
        else if (cp < 0x10000)   { snprintf(tmp, sizeof tmp, "\\u%04x", cp); sb_add(sb, tmp); }
        else                     { snprintf(tmp, sizeof tmp, "\\U%08x", cp); sb_add(sb, tmp); }
        i += (size_t)L;
    }
    sb_addc(sb, q);
}

static int espaco_cp(unsigned c) {
    return (c >= 9 && c <= 13) || (c >= 0x1c && c <= 0x20) || c == 0x85 || c == 0xA0 ||
           c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 ||
           c == 0x202F || c == 0x205F || c == 0x3000;
}

static Str aparar_espacos(Str s) {
    size_t i = 0, j = s.n;
    while (i < j) {
        int L; unsigned cp = cp_decode(s.p, i, j, &L);
        if (!espaco_cp(cp)) break;
        i += (size_t)L;
    }
    while (j > i) {
        size_t k = j - 1;
        while (k > i && ((unsigned char)s.p[k] & 0xC0) == 0x80) k--;
        int L; unsigned cp = cp_decode(s.p, k, j, &L);
        if (!espaco_cp(cp)) break;
        j = k;
    }
    Str r; r.p = xmemdup(s.p + i, j - i); r.n = j - i;
    return r;
}

static void json_str(StrBuf *sb, const char *s, size_t n) {
    sb_addc(sb, '"');
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"':  sb_add(sb, "\\\""); break;
            case '\\': sb_add(sb, "\\\\"); break;
            case '\n': sb_add(sb, "\\n"); break;
            case '\r': sb_add(sb, "\\r"); break;
            case '\t': sb_add(sb, "\\t"); break;
            case '\b': sb_add(sb, "\\b"); break;
            case '\f': sb_add(sb, "\\f"); break;
            default:
                if (c < 0x20) { char t[8]; snprintf(t, sizeof t, "\\u%04x", c); sb_add(sb, t); }
                else sb_addc(sb, (char)c);
        }
    }
    sb_addc(sb, '"');
}

//Tabela de simbolos
#define ERRO "<erro>"
#define CAT_VARIAVEL  "variavel"
#define CAT_PARAMETRO "parametro"
#define CAT_VETOR     "vetor"
#define CAT_FUNCAO    "funcao"
#define RETORNA 1
#define PODE_CONTINUAR 0

static int E(const char *a, const char *b) { return strcmp(a, b) == 0; }

struct Simbolo {
    char       *nome;
    const char *categoria;
    char       *tipo;
    int         e_funcao;
    int         np;
    char      **ptipo;
    char      **pnome;
    char       *tipo_retorno;
    PV         *no;
    int         linha, coluna, visivel_desde;
    BI          tamanho_const;
};

typedef struct Escopo {
    struct Escopo *pai;
    Simbolo      **s;
    int            n, cap;
} Escopo;

static Escopo *esc_novo(Escopo *pai) {
    Escopo *e = xmalloc(sizeof(Escopo));
    memset(e, 0, sizeof *e);
    e->pai = pai;
    return e;
}
static void esc_declarar(Escopo *e, Simbolo *s) {
    if (e->n == e->cap) {
        e->cap = e->cap ? e->cap * 2 : 8;
        e->s = xrealloc(e->s, (size_t)e->cap * sizeof(Simbolo *));
    }
    e->s[e->n++] = s;
}
static Simbolo *esc_local(Escopo *e, const char *nome) {
    for (int i = 0; i < e->n; i++) if (E(e->s[i]->nome, nome)) return e->s[i];
    return NULL;
}
static Simbolo *esc_buscar(Escopo *e, const char *nome, int ordem) {
    for (; e; e = e->pai) {
        Simbolo *s = esc_local(e, nome);
        if (s && s->visivel_desde <= ordem) return s;
    }
    return NULL;
}
static Simbolo *esc_buscar_qualquer(Escopo *e, const char *nome) {
    for (; e; e = e->pai) {
        Simbolo *s = esc_local(e, nome);
        if (s) return s;
    }
    return NULL;
}

//Diagnosticos
typedef struct { const char *codigo; int linha, coluna, seq; Str msg; } Diag;
static Diag *diags; static int ndiags, capdiags, seq_global;

static void erro(const char *codigo, int linha, int coluna, Str msg) {
    if (ndiags == capdiags) {
        capdiags = capdiags ? capdiags * 2 : 16;
        diags = xrealloc(diags, (size_t)capdiags * sizeof(Diag));
    }
    Diag d; d.codigo = codigo; d.linha = linha; d.coluna = coluna; d.msg = msg; d.seq = ++seq_global;
    diags[ndiags++] = d;
}
static int cmp_diag(const void *a, const void *b) {
    const Diag *x = a, *y = b;
    if (x->linha != y->linha)   return x->linha < y->linha ? -1 : 1;
    if (x->coluna != y->coluna) return x->coluna < y->coluna ? -1 : 1;
    return x->seq < y->seq ? -1 : (x->seq > y->seq);
}

//Parametros de vetor
typedef struct { int l, c; } PosVet;
static PosVet *vet_params; static int n_vet_params, cap_vet_params;

static void registrar_param_vetor(int l, int c) {
    if (n_vet_params == cap_vet_params) {
        cap_vet_params = cap_vet_params ? cap_vet_params * 2 : 8;
        vet_params = xrealloc(vet_params, (size_t)cap_vet_params * sizeof(PosVet));
    }
    vet_params[n_vet_params].l = l; vet_params[n_vet_params].c = c; n_vet_params++;
}
static int param_e_vetor(int l, int c) {
    for (int i = 0; i < n_vet_params; i++) if (vet_params[i].l == l && vet_params[i].c == c) return 1;
    return 0;
}
static void liberar_token(Token *t) { free(t->token); free(t->lexema); free(t->attr_string); }

static void normalizar_tokens(VetorTokens *v) {
    int em_params = 0, pdepth = 0, j = 0;
    for (int i = 0; i < v->tamanho; i++) {
        Token *t = &v->dados[i];
        if (em_params && pdepth == 1 && j >= 1 && i + 3 < v->tamanho &&
            (strcmp(v->dados[j - 1].token, "LPAREN") == 0 || strcmp(v->dados[j - 1].token, "COMMA") == 0) &&
            token_em(t, TIPOS) &&
            strcmp(v->dados[i + 1].token, "IDENT") == 0 &&
            strcmp(v->dados[i + 2].token, "LBRACKET") == 0 &&
            strcmp(v->dados[i + 3].token, "RBRACKET") == 0) {
            registrar_param_vetor(t->linha, t->coluna);
            liberar_token(&v->dados[i + 2]); liberar_token(&v->dados[i + 3]);
            v->dados[j++] = v->dados[i];
            v->dados[j++] = v->dados[i + 1];
            i += 3;
            continue;
        }
        v->dados[j++] = *t;
        const char *k = v->dados[j - 1].token;
        if (strcmp(k, "LPAREN") == 0) {
            if (j >= 3 && strcmp(v->dados[j - 2].token, "IDENT") == 0 && token_em(&v->dados[j - 3], TIPOS)) {
                em_params = 1; pdepth = 1;
            } else if (em_params) pdepth++;
        } else if (strcmp(k, "RPAREN") == 0) {
            if (em_params && --pdepth <= 0) { em_params = 0; pdepth = 0; }
        } else if (em_params && (strcmp(k, "SEMICOLON") == 0 || strcmp(k, "LBRACE") == 0 ||
                                 strcmp(k, "RBRACE") == 0 || strcmp(k, "EOF") == 0)) {
            em_params = 0; pdepth = 0;
        }
    }
    v->tamanho = j;
}

//Conversao Node para dicts ordenados
static PV *conv(Node *n);
static PV *conv_lista(NodeList *l) {
    PV *r = pv_list();
    for (int i = 0; i < l->tamanho; i++) pv_push(r, conv(l->dados[i]));
    return r;
}
static PV *no_base(const char *kind, Node *n) {
    PV *d = pv_dict();
    pv_set(d, "node", pv_cstr(kind));
    pv_set(d, "linha", pv_int(n->linha));
    pv_set(d, "coluna", pv_int(n->coluna));
    return d;
}
static PV *conv(Node *n) {
    if (!n) return PVNONE;
    PV *d;
    switch (n->kind) {
    case N_PROGRAM:
        d = pv_dict();
        pv_set(d, "node", pv_cstr("Program"));
        pv_set(d, "declaracoes", conv_lista(&n->lista));
        return d;
    case N_FUNCTION:
        d = no_base("Function", n);
        pv_set(d, "tipo", pv_cstr(n->tipo)); pv_set(d, "nome", pv_cstr(n->nome));
        pv_set(d, "params", conv_lista(&n->lista)); pv_set(d, "corpo", conv(n->f1));
        return d;
    case N_PARAM:
        d = no_base("Param", n);
        pv_set(d, "tipo", pv_cstr(n->tipo)); pv_set(d, "nome", pv_cstr(n->nome));
        pv_set(d, "vetor", pv_bool(param_e_vetor(n->linha, n->coluna)));
        return d;
    case N_VARDECL:
        d = no_base("VarDecl", n);
        pv_set(d, "tipo", pv_cstr(n->tipo)); pv_set(d, "nome", pv_cstr(n->nome));
        pv_set(d, "tamanho", conv(n->f1)); pv_set(d, "valor", conv(n->f2));
        return d;
    case N_BLOCK:
        d = no_base("Block", n); pv_set(d, "itens", conv_lista(&n->lista)); return d;
    case N_IF:
        d = no_base("If", n);
        pv_set(d, "condicao", conv(n->f1)); pv_set(d, "entao", conv(n->f2)); pv_set(d, "senao", conv(n->f3));
        return d;
    case N_WHILE:
        d = no_base("While", n); pv_set(d, "condicao", conv(n->f1)); pv_set(d, "corpo", conv(n->f2)); return d;
    case N_FOR:
        d = no_base("For", n);
        pv_set(d, "init", conv(n->f1)); pv_set(d, "condicao", conv(n->f2));
        pv_set(d, "incremento", conv(n->f3)); pv_set(d, "corpo", conv(n->f4));
        return d;
    case N_RETURN:   d = no_base("Return", n);   pv_set(d, "valor", conv(n->f1)); return d;
    case N_BREAK:    return no_base("Break", n);
    case N_CONTINUE: return no_base("Continue", n);
    case N_PRINT:    d = no_base("Print", n);    pv_set(d, "valor", conv(n->f1)); return d;
    case N_READ:     d = no_base("Read", n);     pv_set(d, "alvo", conv(n->f1)); return d;
    case N_EXPRSTMT: d = no_base("ExprStmt", n); pv_set(d, "expressao", conv(n->f1)); return d;
    case N_ASSIGN:
        d = no_base("Assign", n); pv_set(d, "alvo", conv(n->f1)); pv_set(d, "valor", conv(n->f2)); return d;
    case N_BINARY:
        d = no_base("Binary", n); pv_set(d, "op", pv_cstr(n->op));
        pv_set(d, "esquerda", conv(n->f1)); pv_set(d, "direita", conv(n->f2)); return d;
    case N_UNARY:
        d = no_base("Unary", n); pv_set(d, "op", pv_cstr(n->op)); pv_set(d, "operando", conv(n->f1)); return d;
    case N_CALL:
        d = no_base("Call", n); pv_set(d, "callee", conv(n->f1)); pv_set(d, "argumentos", conv_lista(&n->lista)); return d;
    case N_INDEX:
        d = no_base("Index", n); pv_set(d, "base", conv(n->f1)); pv_set(d, "indice", conv(n->f2)); return d;
    case N_ID:
        d = no_base("Id", n); pv_set(d, "nome", pv_cstr(n->nome)); return d;
    case N_LIT: {
        d = no_base("Lit", n);
        pv_set(d, "tipo", pv_cstr(n->tipo));
        if (E(n->tipo, "int")) {
            const char *t = n->texto;
            while (t[0] == '0' && t[1] != '\0') t++;
            pv_set(d, "valor", pv_int_s(t));
        } else if (E(n->tipo, "float")) {
            pv_set(d, "valor", pv_float(strtod(n->texto, NULL)));
        } else if (E(n->tipo, "bool")) {
            pv_set(d, "valor", pv_bool(E(n->texto, "true")));
        } else {
            pv_set(d, "valor", pv_str(n->texto, n->texto_len));
        }
        return d;
    }
    }
    return PVNONE;
}

//Serializacao
static PV *sym_resumo(Simbolo *s) {
    PV *d = pv_dict();
    pv_set(d, "nome", pv_cstr(s->nome));
    pv_set(d, "categoria", pv_cstr(s->categoria));
    pv_set(d, "tipo", pv_cstr(s->tipo));
    pv_set(d, "linha", pv_int(s->linha));
    pv_set(d, "coluna", pv_int(s->coluna));
    if (s->e_funcao) {
        PV *ps = pv_list();
        for (int i = 0; i < s->np; i++) {
            PV *p = pv_list();
            pv_push(p, pv_cstr(s->ptipo[i])); pv_push(p, pv_cstr(s->pnome[i]));
            pv_push(ps, p);
        }
        pv_set(d, "parametros", ps);
        pv_set(d, "tipo_retorno", pv_cstr(s->tipo_retorno));
    }
    return d;
}

static void indent(StrBuf *sb, int n) { for (int i = 0; i < n; i++) sb_add(sb, "  "); }

static void pv_json(StrBuf *sb, PV *v, int lvl) {
    switch (v->k) {
    case PV_NONE:  sb_add(sb, "null"); break;
    case PV_BOOL:  sb_add(sb, v->b ? "true" : "false"); break;
    case PV_INT:   sb_add_n(sb, v->s, v->sn); break;
    case PV_FLOAT:
        if (isnan(v->f)) sb_add(sb, "NaN");
        else if (isinf(v->f)) sb_add(sb, v->f < 0 ? "-Infinity" : "Infinity");
        else { char *t = python_repr_float(v->f); sb_add(sb, t); free(t); }
        break;
    case PV_STR:   json_str(sb, v->s, v->sn); break;
    case PV_LIST: case PV_TUPLE:
        if (v->n == 0) { sb_add(sb, "[]"); break; }
        sb_add(sb, "[\n");
        for (int i = 0; i < v->n; i++) {
            indent(sb, lvl + 1); pv_json(sb, v->v[i], lvl + 1);
            sb_add(sb, i + 1 < v->n ? ",\n" : "\n");
        }
        indent(sb, lvl); sb_addc(sb, ']');
        break;
    case PV_DICT:
        if (v->n == 0) { sb_add(sb, "{}"); break; }
        sb_add(sb, "{\n");
        for (int i = 0; i < v->n; i++) {
            indent(sb, lvl + 1); json_str(sb, v->keys[i], strlen(v->keys[i])); sb_add(sb, ": ");
            pv_json(sb, v->v[i], lvl + 1);
            sb_add(sb, i + 1 < v->n ? ",\n" : "\n");
        }
        indent(sb, lvl); sb_addc(sb, '}');
        break;
    case PV_SYM: pv_json(sb, sym_resumo(v->sym), lvl); break;
    }
}

static void pv_repr(StrBuf *sb, PV *v) {
    switch (v->k) {
    case PV_NONE:  sb_add(sb, "None"); break;
    case PV_BOOL:  sb_add(sb, v->b ? "True" : "False"); break;
    case PV_INT:   sb_add_n(sb, v->s, v->sn); break;
    case PV_FLOAT: { char *t = python_repr_float(v->f); sb_add(sb, t); free(t); break; }
    case PV_STR:   repr_str(sb, v->s, v->sn); break;
    case PV_LIST: case PV_TUPLE:
        sb_addc(sb, v->k == PV_LIST ? '[' : '(');
        for (int i = 0; i < v->n; i++) { if (i) sb_add(sb, ", "); pv_repr(sb, v->v[i]); }
        if (v->k == PV_TUPLE && v->n == 1) sb_addc(sb, ',');
        sb_addc(sb, v->k == PV_LIST ? ']' : ')');
        break;
    case PV_DICT:
        sb_addc(sb, '{');
        for (int i = 0; i < v->n; i++) {
            if (i) sb_add(sb, ", ");
            repr_str(sb, v->keys[i], strlen(v->keys[i])); sb_add(sb, ": ");
            pv_repr(sb, v->v[i]);
        }
        sb_addc(sb, '}');
        break;
    case PV_SYM: {
        Simbolo *s = v->sym;
        sb_add(sb, "Simbolo(nome="); repr_str(sb, s->nome, strlen(s->nome));
        sb_add(sb, ", categoria="); repr_str(sb, s->categoria, strlen(s->categoria));
        sb_add(sb, ", tipo="); repr_str(sb, s->tipo, strlen(s->tipo));
        sb_add(sb, ", parametros=");
        if (!s->e_funcao) sb_add(sb, "None");
        else {
            sb_addc(sb, '[');
            for (int i = 0; i < s->np; i++) {
                if (i) sb_add(sb, ", ");
                sb_addc(sb, '(');
                repr_str(sb, s->ptipo[i], strlen(s->ptipo[i])); sb_add(sb, ", ");
                repr_str(sb, s->pnome[i], strlen(s->pnome[i]));
                sb_addc(sb, ')');
            }
            sb_addc(sb, ']');
        }
        sb_add(sb, ", tipo_retorno=");
        if (s->tipo_retorno) repr_str(sb, s->tipo_retorno, strlen(s->tipo_retorno)); else sb_add(sb, "None");
        sb_add(sb, ", linha="); sb_add_int(sb, s->linha);
        sb_add(sb, ", coluna="); sb_add_int(sb, s->coluna);
        sb_add(sb, ", visivel_desde="); sb_add_int(sb, s->visivel_desde);
        sb_add(sb, ", tamanho_const=");
        if (s->tamanho_const.ok) { char *t = bi_str(s->tamanho_const); sb_add(sb, t); free(t); } else sb_add(sb, "None");
        sb_addc(sb, ')');
        break;
    }
    }
}

static Str pv_fstr(PV *v) {
    StrBuf sb = {0};
    switch (v->k) {
    case PV_STR:   sb_add_n(&sb, v->s, v->sn); break;
    case PV_INT:   sb_add_n(&sb, v->s, v->sn); break;
    default:       pv_repr(&sb, v); break;
    }
    return str_from_sb(&sb);
}

//Acesso a nos
static PV *G(PV *n, const char *k)             { return pv_get(n, k); }
static int is_none(PV *v)                      { return v == NULL || v->k == PV_NONE; }
static const char *GS(PV *n, const char *k)    { return pv_get(n, k)->s; }
static int GI(PV *n, const char *k)            { return atoi(pv_get(n, k)->s); }
static const char *K(PV *n)                    { return pv_get(n, "node")->s; }
static PV *sem(PV *n) {
    PV *s = pv_get(n, "sem");
    if (!s) { s = pv_dict(); pv_set(n, "sem", s); }
    return s;
}
#define SSET(n, k, v) pv_set(sem(n), (k), (v))

typedef struct { int l, c; } Pos;
static Pos inicio(PV *n) {
    const char *k = K(n);
    if (E(k, "Binary")) return inicio(G(n, "esquerda"));
    if (E(k, "Call"))   return inicio(G(n, "callee"));
    if (E(k, "Index"))  return inicio(G(n, "base"));
    if (E(k, "Assign")) return inicio(G(n, "alvo"));
    Pos p = { GI(n, "linha"), GI(n, "coluna") };
    return p;
}

//Inteiros grandes
static BI const_int(PV *n) {
    BI r = { 0, 0, NULL };
    if (is_none(n)) return r;
    const char *k = K(n);
    if (E(k, "Lit") && E(GS(n, "tipo"), "int")) {
        r.ok = 1; r.neg = 0; r.d = G(n, "valor")->s; return r;
    }
    if (E(k, "Unary") && E(GS(n, "op"), "-")) {
        BI v = const_int(G(n, "operando"));
        if (!v.ok) return r;
        r = v;
        if (!(v.d[0] == '0' && v.d[1] == '\0')) r.neg = !v.neg;
        return r;
    }
    return r;
}
static int bi_zero(BI b) { return b.d[0] == '0' && b.d[1] == '\0'; }
static int bi_mag_cmp(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    if (la != lb) return la < lb ? -1 : 1;
    int c = strcmp(a, b);
    return c < 0 ? -1 : (c > 0);
}
static BI bi_dec_pos(BI b) {
    BI r = b;
    size_t n = strlen(b.d);
    char *d = xmemdup(b.d, n);
    int i = (int)n - 1;
    while (i >= 0) {
        if (d[i] == '0') { d[i] = '9'; i--; } else { d[i]--; break; }
    }
    char *p = d;
    while (p[0] == '0' && p[1] != '\0') p++;
    r.d = xstrdup(p);
    return r;
}

//Tipos
static int is_num(const char *t) { return E(t, "int") || E(t, "float"); }
static int tipovar_n(const char *s, size_t n) {
    return (n == 3 && !strncmp(s, "int", 3)) || (n == 5 && !strncmp(s, "float", 5)) ||
           (n == 4 && !strncmp(s, "bool", 4)) || (n == 4 && !strncmp(s, "char", 4));
}
static int compat(const char *dest, const char *orig, const char **conv_out) {
    size_t ld = strlen(dest);
    if (conv_out) *conv_out = NULL;
    if (E(dest, orig) &&
        (tipovar_n(dest, ld) || (ld > 2 && !strcmp(dest + ld - 2, "[]") && tipovar_n(dest, ld - 2))))
        return 1;
    if (E(dest, "float") && E(orig, "int")) { if (conv_out) *conv_out = "int->float"; return 1; }
    return 0;
}
static char *vec_tipo(const char *t) { return mk("%s[]", t).p; }
static Str plural(int n, const char *palavra) { return mk("%d %s%s", n, palavra, n == 1 ? "" : "s"); }
static const char *desc_cat(const char *c) {
    if (E(c, CAT_VARIAVEL))  return "uma variável";
    if (E(c, CAT_PARAMETRO)) return "um parâmetro";
    if (E(c, CAT_VETOR))     return "um vetor";
    return "uma função";
}
static char *assinatura(Simbolo *s) {
    StrBuf sb = {0};
    sb_addc(&sb, '(');
    for (int i = 0; i < s->np; i++) { if (i) sb_addc(&sb, ','); sb_add(&sb, s->ptipo[i]); }
    sb_add(&sb, ")->");
    sb_add(&sb, s->tipo_retorno);
    return sb.dados;
}

//Texto de expressao
static Str texto_expr(PV *n) {
    if (is_none(n) || n->k != PV_DICT) return mk("");
    const char *k = K(n);
    if (E(k, "Lit")) {
        PV *v = G(n, "valor");
        if (v->k == PV_BOOL) return mk("%s", v->b ? "true" : "false");
        return pv_fstr(v);
    }
    if (E(k, "Id")) return mk("%s", GS(n, "nome"));
    if (E(k, "Binary")) {
        Str r = mk("%S %s %S", texto_expr(G(n, "esquerda")), GS(n, "op"), texto_expr(G(n, "direita")));
        return aparar_espacos(r);
    }
    if (E(k, "Unary"))  return mk("%s%S", GS(n, "op"), texto_expr(G(n, "operando")));
    if (E(k, "Assign")) return mk("%S = %S", texto_expr(G(n, "alvo")), texto_expr(G(n, "valor")));
    if (E(k, "Index"))  return mk("%S[%S]", texto_expr(G(n, "base")), texto_expr(G(n, "indice")));
    if (E(k, "Call")) {
        StrBuf sb = {0};
        Str c = texto_expr(G(n, "callee"));
        sb_add_n(&sb, c.p, c.n); sb_addc(&sb, '(');
        PV *args = G(n, "argumentos");
        for (int i = 0; i < args->n; i++) {
            if (i) sb_add(&sb, ", ");
            Str a = texto_expr(args->v[i]); sb_add_n(&sb, a.p, a.n);
        }
        sb_addc(&sb, ')');
        return str_from_sb(&sb);
    }
    return mk("");
}

static Str get_valor_ou_vazio(PV *n) {
    PV *v = G(n, "valor");
    return v ? pv_fstr(v) : mk("");
}

//Analisador semantico
typedef struct { const char *nome; const char *tipo_retorno; int loop_depth; } Ctx;

static Escopo  *global_scope, *escopo;
static Ctx      ctx;
static int      ordem_atual;
static Simbolo *decl_atual;

static const char *expr(PV *n, int permitir_void);
static int stmt(PV *n);
static Simbolo *resolver_(PV *n);

static void abrir(void)  { escopo = esc_novo(escopo); }
static void fechar(void) { escopo = escopo->pai; }

static int declarar(Simbolo *sym) {
    Simbolo *ant = esc_local(escopo, sym->nome);
    if (ant) {
        int col_nome = sym->coluna + (int)strlen(GS(sym->no, "tipo")) + 1;
        int decl_col = ant->coluna + (int)strlen(GS(ant->no, "tipo")) + 1;
        erro("SEM002", sym->linha, col_nome,
             mk("“%s” já declarado neste escopo; declaração anterior na linha %d, coluna %d (tipo %s).",
                sym->nome, ant->linha, decl_col, ant->tipo));
        return 0;
    }
    esc_declarar(escopo, sym);
    return 1;
}

static Simbolo *simbolo_var(PV *d, int parametro, int visivel_desde) {
    const char *tipo = GS(d, "tipo");
    if (E(tipo, "void")) {
        erro("SEM003", GI(d, "linha"), GI(d, "coluna"),
             mk("%s \"%s\" não pode ter tipo void.", parametro ? "Parâmetro" : "Variável", GS(d, "nome")));
        tipo = ERRO;
    }
    const char *cat;
    PV *vet = G(d, "vetor");
    if (parametro) cat = (vet && vet->k == PV_BOOL && vet->b) ? CAT_VETOR : CAT_PARAMETRO;
    else if (!is_none(G(d, "tamanho"))) cat = CAT_VETOR;
    else cat = CAT_VARIAVEL;
    Simbolo *s = xmalloc(sizeof(Simbolo));
    memset(s, 0, sizeof *s);
    s->nome = xstrdup(GS(d, "nome")); s->categoria = cat; s->tipo = xstrdup(tipo);
    s->no = d; s->linha = GI(d, "linha"); s->coluna = GI(d, "coluna"); s->visivel_desde = visivel_desde;
    if (E(cat, CAT_VETOR)) {
        PV *t = G(d, "tamanho");
        s->tamanho_const = const_int(is_none(t) ? NULL : t);
    }
    return s;
}

static Simbolo *simbolo_funcao(PV *d, int visivel_desde) {
    Simbolo *s = xmalloc(sizeof(Simbolo));
    memset(s, 0, sizeof *s);
    PV *ps = G(d, "params");
    s->e_funcao = 1; s->np = ps->n;
    s->ptipo = xmalloc((size_t)(ps->n + 1) * sizeof(char *));
    s->pnome = xmalloc((size_t)(ps->n + 1) * sizeof(char *));
    for (int i = 0; i < ps->n; i++) {
        PV *p = ps->v[i];
        const char *tp = E(GS(p, "tipo"), "void") ? ERRO : GS(p, "tipo");
        PV *vet = G(p, "vetor");
        if (!E(tp, ERRO) && vet && vet->k == PV_BOOL && vet->b) s->ptipo[i] = vec_tipo(tp);
        else s->ptipo[i] = xstrdup(tp);
        s->pnome[i] = xstrdup(GS(p, "nome"));
    }
    s->nome = xstrdup(GS(d, "nome")); s->categoria = CAT_FUNCAO; s->tipo = xstrdup(GS(d, "tipo"));
    s->tipo_retorno = xstrdup(GS(d, "tipo"));
    s->no = d; s->linha = GI(d, "linha"); s->coluna = GI(d, "coluna"); s->visivel_desde = visivel_desde;
    return s;
}

static void analisar_inicializacao(PV *d, Simbolo *sym) {
    SSET(d, "simbolo", pv_sym(sym));
    SSET(d, "tipo_declarado", pv_cstr(sym->tipo));
    PV *tam = G(d, "tamanho");
    if (!is_none(tam)) {
        const char *ts = expr(tam, 0);
        if (!E(ts, ERRO) && !E(ts, "int")) {
            Pos p = inicio(tam);
            erro("SEM006", p.l, p.c, mk("Tamanho do vetor deve ser int; recebeu %s.", ts));
        } else if (sym->tamanho_const.ok && (sym->tamanho_const.neg || bi_zero(sym->tamanho_const))) {
            Pos p = inicio(tam);
            erro("SEM006", p.l, p.c,
                 mk("Tamanho do vetor \"%s\" deve ser positivo; recebeu %B.", GS(d, "nome"), sym->tamanho_const));
        }
    }
    PV *val = G(d, "valor");
    if (!is_none(val)) {
        const char *tv = expr(val, 0);
        SSET(d, "tipo_valor", pv_cstr(tv));
        if (!E(tv, ERRO) && !E(sym->tipo, ERRO)) {
            const char *conv_s;
            int ok = compat(sym->tipo, tv, &conv_s);
            SSET(d, "compativel", pv_bool(ok));
            SSET(d, "conversao", pv_cstr(conv_s));
            if (!ok) {
                Pos p = inicio(val);
                erro("SEM003", p.l, p.c,
                     mk("Não é possível atribuir %s a %s sem conversão permitida (destino “%s”; expressão “%S”).",
                        tv, sym->tipo, GS(d, "nome"), get_valor_ou_vazio(val)));
            }
        }
    }
}

static int declaracao_local(PV *d) {
    Simbolo *sym = simbolo_var(d, 0, 0);
    analisar_inicializacao(d, sym);
    declarar(sym);
    return PODE_CONTINUAR;
}

static const char *achar_cond(PV *n) {
    if (!n) return NULL;
    if (n->k == PV_DICT) {
        PV *nd = pv_get(n, "node");
        if (nd && nd->k == PV_STR && E(nd->s, "If")) {
            PV *c = pv_get(n, "condicao");
            if (c && c->k == PV_DICT) {
                PV *cn = pv_get(c, "node");
                if (cn && cn->k == PV_STR && E(cn->s, "Id")) return pv_get(c, "nome")->s;
            }
        }
        for (int i = 0; i < n->n; i++) { const char *r = achar_cond(n->v[i]); if (r && r[0]) return r; }
    } else if (n->k == PV_LIST || n->k == PV_TUPLE) {
        for (int i = 0; i < n->n; i++) { const char *r = achar_cond(n->v[i]); if (r && r[0]) return r; }
    }
    return NULL;
}

static int itens(PV *lista) {
    int fluxo = PODE_CONTINUAR;
    for (int i = 0; i < lista->n; i++) if (stmt(lista->v[i]) == RETORNA) fluxo = RETORNA;
    return fluxo;
}

static void analisar_funcao(PV *d, Simbolo *sym) {
    SSET(d, "simbolo", pv_sym(sym));
    SSET(d, "assinatura", pv_cstr(assinatura(sym)));
    ctx.nome = sym->nome; ctx.tipo_retorno = GS(d, "tipo"); ctx.loop_depth = 0;
    abrir();
    PV *ps = G(d, "params");
    for (int i = 0; i < ps->n; i++) {
        Simbolo *psym = simbolo_var(ps->v[i], 1, 0);
        SSET(ps->v[i], "simbolo", pv_sym(psym));
        declarar(psym);
    }
    int fluxo = itens(G(G(d, "corpo"), "itens"));
    if (!E(GS(d, "tipo"), "void") && fluxo != RETORNA) {
        const char *cn = achar_cond(G(d, "corpo"));
        if (!cn) cn = "condicao";
        erro("SEM011", GI(d, "linha"), GI(d, "coluna"),
             mk("A função “%s” pode terminar sem retornar %s; o ramo em que “%s” é falso alcança o fim do corpo.",
                GS(d, "nome"), GS(d, "tipo"), cn));
    }
    fechar();
    ctx.nome = NULL; ctx.tipo_retorno = NULL; ctx.loop_depth = 0;
}

static void condicao(PV *e, PV *pai) {
    const char *t = expr(e, 0);
    SSET(pai, "tipo_condicao", pv_cstr(t));
    if (!E(t, ERRO) && !E(t, "bool")) {
        const char *pk = K(pai);
        const char *nc = E(pk, "If") ? "if" : E(pk, "While") ? "while" : E(pk, "For") ? "for" : "comando";
        Str txt;
        PV *nm = G(e, "nome");
        if (nm) txt = pv_fstr(nm);
        else    txt = get_valor_ou_vazio(e);
        Pos p = inicio(e);
        erro("SEM005", p.l, p.c,
             mk("Condição de %s deve ter tipo bool; recebeu %s (expressão “%S”).", nc, t, txt));
    }
}

static void corpo_laco(PV *n) {
    ctx.loop_depth++;
    stmt(G(n, "corpo"));
    ctx.loop_depth--;
}

static int e_return(PV *n) {
    int pl = GI(n, "linha"), pc = GI(n, "coluna");
    PV *valor = G(n, "valor");
    const char *tv = !is_none(valor) ? expr(valor, 0) : NULL;
    SSET(n, "tipo_esperado", pv_cstr(ctx.tipo_retorno));
    SSET(n, "tipo_efetivo", pv_cstr(is_none(valor) ? "void" : tv));
    if (!ctx.nome) {
        erro("SEM010", pl, pc, mk("return só pode ocorrer dentro de uma função."));
        return PODE_CONTINUAR;
    }
    const char *esp = ctx.tipo_retorno;
    if (E(esp, "void")) {
        if (!is_none(valor) && !E(tv, ERRO))
            erro("SEM009", pl, pc, mk("Função void \"%s\" não pode retornar valor (recebeu %s).", ctx.nome, tv));
    } else if (is_none(valor)) {
        erro("SEM009", pl, pc, mk("Retorno sem valor em função \"%s\" com tipo %s.", ctx.nome, esp));
    } else if (!E(tv, ERRO)) {
        const char *cv;
        int ok = compat(esp, tv, &cv);
        SSET(n, "conversao", pv_cstr(cv));
        if (!ok) {
            Pos p = inicio(valor);
            erro("SEM009", p.l, p.c,
                 mk("Retorno %s incompatível com o tipo %s da função “%s”; conversão implícita de %s para %s não permitida.",
                    tv, esp, ctx.nome, tv, esp));
        }
    }
    return RETORNA;
}

typedef struct { const char *t; int atrib; } Dest;
static Dest destino(PV *alvo, const char *acao) {
    const char *k = K(alvo);
    Pos pos = inicio(alvo);
    Dest r = { ERRO, 0 };
    if (E(k, "Id")) {
        Simbolo *sym = resolver_(alvo);
        if (!sym) return r;
        if (E(sym->categoria, CAT_VETOR)) {
            erro("SEM003", pos.l, pos.c,
                 mk("Não é possível usar o vetor \"%s\" inteiro como destino de %s; use %s[i].", sym->nome, acao, sym->nome));
            return r;
        }
        if (E(sym->categoria, CAT_FUNCAO)) {
            erro("SEM003", pos.l, pos.c,
                 mk("\"%s\" é uma função e não pode ser destino de %s.", sym->nome, acao));
            return r;
        }
        r.t = sym->tipo; r.atrib = 1;
        return r;
    }
    if (E(k, "Index")) { r.t = expr(alvo, 0); r.atrib = 1; return r; }
    expr(alvo, 1);
    const char *desc = E(k, "Lit") ? "literal" : E(k, "Call") ? "chamada de função" : "expressão";
    if (E(acao, "atribuição") && E(k, "Lit") && E(GS(alvo, "tipo"), "int")) {
        erro("SEM013", pos.l, pos.c,
             mk("Destino de atribuição não é atribuível; o literal inteiro “%S” não designa uma variável ou elemento de vetor.",
                pv_fstr(G(alvo, "valor"))));
    } else {
        erro("SEM003", pos.l, pos.c, mk("Destino de %s inválido: %s não é atribuível.", acao, desc));
    }
    return r;
}

static int stmt(PV *n) {
    const char *k = K(n);
    if (E(k, "VarDecl")) return declaracao_local(n);
    if (E(k, "Block")) { abrir(); int f = itens(G(n, "itens")); fechar(); return f; }
    if (E(k, "ExprStmt")) {
        PV *e = G(n, "expressao");
        if (!is_none(e)) expr(e, 1);
        return PODE_CONTINUAR;
    }
    if (E(k, "If")) {
        condicao(G(n, "condicao"), n);
        int f1 = stmt(G(n, "entao"));
        if (!is_none(G(n, "senao"))) {
            int f2 = stmt(G(n, "senao"));
            return (f1 == RETORNA && f2 == RETORNA) ? RETORNA : PODE_CONTINUAR;
        }
        return PODE_CONTINUAR;
    }
    if (E(k, "While")) { condicao(G(n, "condicao"), n); corpo_laco(n); return PODE_CONTINUAR; }
    if (E(k, "For")) {
        abrir();
        PV *init = G(n, "init");
        if (!is_none(init)) stmt(init);
        if (!is_none(G(n, "condicao"))) condicao(G(n, "condicao"), n);
        if (!is_none(G(n, "incremento"))) expr(G(n, "incremento"), 1);
        corpo_laco(n);
        fechar();
        return PODE_CONTINUAR;
    }
    if (E(k, "Return")) return e_return(n);
    if (E(k, "Break") || E(k, "Continue")) {
        SSET(n, "loop_depth", pv_int(ctx.loop_depth));
        if (ctx.loop_depth == 0) {
            const char *pal = E(k, "Break") ? "break" : "continue";
            erro("SEM010", GI(n, "linha"), GI(n, "coluna"), mk("%s só pode ocorrer dentro de um laço.", pal));
        }
        return PODE_CONTINUAR;
    }
    if (E(k, "Print")) { expr(G(n, "valor"), 0); return PODE_CONTINUAR; }
    if (E(k, "Read")) {
        Dest d = destino(G(n, "alvo"), "leitura");
        SSET(n, "tipo_alvo", pv_cstr(d.t));
        return PODE_CONTINUAR;
    }
    return PODE_CONTINUAR;
}

//Expressoes
static Simbolo *resolver_(PV *n) {
    const char *nome = GS(n, "nome");
    Simbolo *sym = esc_buscar(escopo, nome, ordem_atual);
    PV *sm = sem(n);
    if (!sym) {
        Simbolo *outro = esc_buscar_qualquer(escopo, nome);
        Str msg;
        if (outro && outro == decl_atual)
            msg = mk("Identificador \"%s\" não pode ser usado na própria inicialização.", nome);
        else if (outro)
            msg = mk("Identificador \"%s\" não declarado neste escopo (declarado somente mais adiante, na linha %d).",
                     nome, outro->linha);
        else
            msg = mk("Identificador “%s” não declarado neste escopo.", nome);
        erro("SEM001", GI(n, "linha"), GI(n, "coluna"), msg);
        pv_set(sm, "simbolo", PVNONE); pv_set(sm, "categoria", PVNONE); pv_set(sm, "tipo", pv_cstr(ERRO));
        return NULL;
    }
    const char *tipo = sym->tipo;
    if (E(sym->categoria, CAT_VETOR)) tipo = vec_tipo(sym->tipo);
    else if (E(sym->categoria, CAT_FUNCAO)) tipo = assinatura(sym);
    pv_set(sm, "simbolo", pv_sym(sym)); pv_set(sm, "categoria", pv_cstr(sym->categoria)); pv_set(sm, "tipo", pv_cstr(tipo));
    return sym;
}

static const char *e_lit(PV *n) {
    SSET(n, "valor", G(n, "valor"));
    return GS(n, "tipo");
}

static const char *e_id(PV *n) {
    Simbolo *sym = resolver_(n);
    if (!sym) return ERRO;
    int l = GI(n, "linha"), c = GI(n, "coluna");
    if (E(sym->categoria, CAT_VETOR)) {
        erro("SEM003", l, c, mk("\"%s\" é um vetor e não pode ser usado como valor; use %s[i] para acessar um elemento.",
                                sym->nome, sym->nome));
        return ERRO;
    }
    if (E(sym->categoria, CAT_FUNCAO)) {
        erro("SEM003", l, c, mk("\"%s\" é uma função e não pode ser usada como valor; use %s(...) para chamá-la.",
                                sym->nome, sym->nome));
        return ERRO;
    }
    return sym->tipo;
}

static const char *e_binary(PV *n) {
    const char *op = GS(n, "op");
    const char *te = expr(G(n, "esquerda"), 0);
    const char *td = expr(G(n, "direita"), 0);
    PV *conversoes = pv_list();
    SSET(n, "tipo_esquerda", pv_cstr(te)); SSET(n, "tipo_direita", pv_cstr(td)); SSET(n, "conversoes", conversoes);
    if (E(te, ERRO) || E(td, ERRO)) return ERRO;
    const char *res = NULL, *exig = "";
    int numericos = is_num(te) && is_num(td);
    if (E(op, "+") || E(op, "-") || E(op, "*") || E(op, "/")) {
        exig = "numéricos";
        if (numericos) res = (E(te, "float") || E(td, "float")) ? "float" : "int";
    } else if (E(op, "%")) {
        exig = "int";
        if (E(te, "int") && E(td, "int")) res = "int";
    } else if (E(op, "<") || E(op, "<=") || E(op, ">") || E(op, ">=")) {
        exig = "numéricos (ou ambos char)";
        if (numericos || (E(te, "char") && E(td, "char"))) res = "bool";
    } else if (E(op, "==") || E(op, "!=")) {
        exig = "numéricos ou do mesmo tipo (bool/char)";
        if (numericos || (E(te, td) && (E(te, "bool") || E(te, "char")))) res = "bool";
    } else if (E(op, "&&") || E(op, "||")) {
        exig = "bool";
        if (E(te, "bool") && E(td, "bool")) res = "bool";
    }
    if (!res) {
        erro("SEM004", GI(n, "linha"), GI(n, "coluna"),
             mk("Operador %s requer operandos %s; recebeu %s e %s.", op, exig, te, td));
        return ERRO;
    }
    if (numericos && !E(te, td))
        pv_push(conversoes, pv_cstr(E(te, "int") ? "esquerda int->float" : "direita int->float"));
    return res;
}

static const char *e_unary(PV *n) {
    const char *op = GS(n, "op");
    const char *t = expr(G(n, "operando"), 0);
    SSET(n, "tipo_operando", pv_cstr(t)); SSET(n, "conversoes", pv_list());
    if (E(t, ERRO)) return ERRO;
    if (E(op, "-") && is_num(t)) return t;
    if (E(op, "!") && E(t, "bool")) return "bool";
    erro("SEM004", GI(n, "linha"), GI(n, "coluna"),
         mk("Operador %s requer operando %s; recebeu %s.", op, E(op, "-") ? "numérico" : "bool", t));
    return ERRO;
}

static const char *e_assign(PV *n) {
    Dest d = destino(G(n, "alvo"), "atribuição");
    const char *td = d.t;
    const char *tv = expr(G(n, "valor"), 0);
    SSET(n, "tipo_destino", pv_cstr(td)); SSET(n, "tipo_valor", pv_cstr(tv));
    SSET(n, "atribuivel", pv_bool(d.atrib)); SSET(n, "compativel", PVNONE); SSET(n, "conversao", PVNONE);
    if (E(td, ERRO) || E(tv, ERRO)) return td;
    const char *cv;
    int ok = compat(td, tv, &cv);
    SSET(n, "compativel", pv_bool(ok)); SSET(n, "conversao", pv_cstr(cv));
    if (!ok) {
        Pos p = inicio(G(n, "valor"));
        erro("SEM003", p.l, p.c,
             mk("Não é possível atribuir %s a %s sem conversão permitida (destino “%S”; expressão “%S”).",
                tv, td, texto_expr(G(n, "alvo")), texto_expr(G(n, "valor"))));
    }
    return td;
}

static const char *e_index(PV *n) {
    PV *base = G(n, "base"), *idx = G(n, "indice");
    Simbolo *sym = NULL;
    const char *elem = ERRO;
    if (E(K(base), "Id")) {
        sym = resolver_(base);
        if (sym) {
            if (E(sym->categoria, CAT_VETOR)) elem = sym->tipo;
            else {
                Pos p = inicio(base);
                erro("SEM006", p.l, p.c,
                     mk("“%s” é %s e não pode ser indexado(a); apenas vetores.", sym->nome, desc_cat(sym->categoria)));
            }
        }
    } else {
        const char *tb = expr(base, 0);
        if (!E(tb, ERRO)) {
            Pos p = inicio(base);
            erro("SEM006", p.l, p.c, mk("Expressão do tipo %s não pode ser indexada.", tb));
        }
    }
    const char *ti = expr(idx, 0);
    if (!E(ti, ERRO) && !E(ti, "int")) {
        Pos p = inicio(idx);
        erro("SEM006", p.l, p.c,
             mk("Índice do vetor “%s” deve ser int; recebeu %s (expressão “%S”).",
                sym ? sym->nome : "", ti, get_valor_ou_vazio(idx)));
    } else if (sym && E(sym->categoria, CAT_VETOR) && sym->tamanho_const.ok &&
               !sym->tamanho_const.neg && !bi_zero(sym->tamanho_const)) {
        BI v = const_int(idx);
        if (v.ok) {
            BI tam = sym->tamanho_const;
            int dentro = (!v.neg || bi_zero(v)) && bi_mag_cmp(v.d, tam.d) < 0;
            if (!dentro) {
                Pos p = inicio(idx);
                erro("SEM006", p.l, p.c,
                     mk("Índice %B fora da faixa do vetor “%s” (tamanho %B; índices válidos 0..%B).",
                        v, sym->nome, tam, bi_dec_pos(tam)));
            }
        }
    }
    SSET(n, "tipo_vetor", pv_cstr(!E(elem, ERRO) ? vec_tipo(elem) : ERRO));
    SSET(n, "tipo_indice", pv_cstr(ti)); SSET(n, "tipo_elemento", pv_cstr(elem));
    return elem;
}

static const char *e_arg(PV *a) {
    if (E(K(a), "Id")) {
        Simbolo *sym = resolver_(a);
        if (!sym) return ERRO;
        if (E(sym->categoria, CAT_VETOR)) return !E(sym->tipo, ERRO) ? vec_tipo(sym->tipo) : ERRO;
    }
    return expr(a, 0);
}

static const char *e_call(PV *n) {
    PV *callee = G(n, "callee"), *args = G(n, "argumentos");
    Simbolo *sym = NULL;
    Pos pos = inicio(n);
    if (E(K(callee), "Id")) {
        Simbolo *s = resolver_(callee);
        if (s) {
            if (E(s->categoria, CAT_FUNCAO)) sym = s;
            else erro("SEM007", pos.l, pos.c,
                      mk("\"%s\" é %s e não uma função; não pode ser chamado(a).", s->nome, desc_cat(s->categoria)));
        }
    } else {
        const char *tc = expr(callee, 0);
        if (!E(tc, ERRO))
            erro("SEM007", pos.l, pos.c, mk("Expressão do tipo %s não pode ser chamada como função.", tc));
    }
    const char **targs = xmalloc((size_t)(args->n + 1) * sizeof(char *));
    PV *lt = pv_list();
    for (int i = 0; i < args->n; i++) { targs[i] = e_arg(args->v[i]); pv_push(lt, pv_cstr(targs[i])); }
    SSET(n, "simbolo", sym ? pv_sym(sym) : PVNONE);
    SSET(n, "tipos_argumentos", lt);
    SSET(n, "assinatura", sym ? pv_cstr(assinatura(sym)) : PVNONE);
    SSET(n, "tipo_retorno", pv_cstr(sym ? sym->tipo_retorno : ERRO));
    if (!sym) return ERRO;
    if (args->n != sym->np) {
        erro("SEM007", pos.l, pos.c,
             mk("“%s” espera %S, mas recebeu %d.", sym->nome, plural(sym->np, "argumento"), args->n));
    } else {
        for (int i = 0; i < sym->np; i++) {
            const char *tp = sym->ptipo[i], *ta = targs[i];
            if (E(tp, ERRO) || E(ta, ERRO)) continue;
            if (!compat(tp, ta, NULL)) {
                Pos p = inicio(args->v[i]);
                erro("SEM008", p.l, p.c,
                     mk("Argumento %d de “%s”: esperado %s, recebido %s (expressão “%S”).",
                        i + 1, sym->nome, tp, ta, texto_expr(args->v[i])));
            }
        }
    }
    return sym->tipo_retorno;
}

static const char *expr(PV *n, int permitir_void) {
    const char *k = K(n), *t;
    if (E(k, "Lit")) t = e_lit(n);
    else if (E(k, "Id")) t = e_id(n);
    else if (E(k, "Binary")) t = e_binary(n);
    else if (E(k, "Unary")) t = e_unary(n);
    else if (E(k, "Assign")) t = e_assign(n);
    else if (E(k, "Index")) t = e_index(n);
    else if (E(k, "Call")) t = e_call(n);
    else t = ERRO;
    if (E(t, "void") && !permitir_void) {
        const char *nome = NULL;
        if (E(k, "Call")) { PV *nm = G(G(n, "callee"), "nome"); if (nm) nome = nm->s; }
        Pos p = inicio(n);
        if (nome)
            erro("SEM012", p.l, p.c,
                 mk("Função “%s” não produz valor (retorno void) e não pode ser usada como expressão de atribuição.", nome));
        else
            erro("SEM012", p.l, p.c,
                 mk("Função não produz valor (retorno void) e não pode ser usada como expressão de atribuição."));
        t = ERRO;
    }
    SSET(n, "tipo", pv_cstr(t));
    return t;
}

//Driver da analise
static void analisar(PV *ast) {
    global_scope = esc_novo(NULL); escopo = global_scope;
    ctx.nome = NULL; ctx.tipo_retorno = NULL; ctx.loop_depth = 0;
    ordem_atual = 0; decl_atual = NULL;
    PV *decls = G(ast, "declaracoes");
    Simbolo **syms = xmalloc((size_t)(decls->n + 1) * sizeof(Simbolo *));
    for (int i = 0; i < decls->n; i++) {
        PV *d = decls->v[i];
        const char *k = K(d);
        syms[i] = NULL;
        if (E(k, "Function"))     syms[i] = simbolo_funcao(d, i);
        else if (E(k, "VarDecl")) syms[i] = simbolo_var(d, 0, i + 1);
        else continue;
        declarar(syms[i]);
    }
    for (int i = 0; i < decls->n; i++) {
        PV *d = decls->v[i];
        ordem_atual = i;
        ctx.nome = NULL; ctx.tipo_retorno = NULL; ctx.loop_depth = 0;
        decl_atual = NULL;
        const char *k = K(d);
        if (E(k, "Function")) analisar_funcao(d, syms[i]);
        else if (E(k, "VarDecl")) { decl_atual = syms[i]; analisar_inicializacao(d, syms[i]); }
        else stmt(d);
    }
    PV *gl = pv_list();
    for (int i = 0; i < global_scope->n; i++) pv_push(gl, sym_resumo(global_scope->s[i]));
    SSET(ast, "simbolos_globais", gl);
    qsort(diags, (size_t)ndiags, sizeof(Diag), cmp_diag);
}

//Pipeline e main
#ifndef MINIC_NO_ENTRY
int minic_entrada(int argc, char *argv[]) __asm__(MINIC_ASM("main"));
int minic_entrada(int argc, char *argv[]) {
    const char *arquivo = NULL;
    int flag_ast = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ast") == 0) flag_ast = 1;
        if (strncmp(argv[i], "--", 2) != 0 && !arquivo) arquivo = argv[i];
    }
    if (!arquivo) {
        fputs("Uso: analisador_semantico <arquivo> [--ast]\n", stderr);
        return 2;
    }
    size_t tamanho = 0;
    char *codigo = ler_arquivo(arquivo, &tamanho);
    if (!codigo) {
        if (!ler_arquivo_utf8_invalido) fprintf(stderr, "Erro ao abrir arquivo: %s\n", arquivo);
        return 1;
    }

    VetorTokens tokens = {0};
    VetorErros erros_lex = {0};
    scanner_n(codigo, (int)tamanho, &tokens, &erros_lex);
    normalizar_tokens(&tokens);
    Parser parser;
    memset(&parser, 0, sizeof parser);
    parser.tokens = tokens.dados;
    parser.ntokens = tokens.tamanho;
    Node *ast_c = parse_programa(&parser);

    StrBuf out = {0};
    int primeira = 1;
#define LINHA_INI() do { if (!primeira) sb_addc(&out, '\n'); primeira = 0; } while (0)

    if (erros_lex.tamanho > 0 || parser.erros.tamanho > 0) {
        for (int i = 0; i < erros_lex.tamanho; i++) {
            Erro *e = &erros_lex.dados[i];
            LINHA_INI();
            sb_add(&out, "ERRO LÉXICO "); sb_add_int(&out, e->linha); sb_addc(&out, ':');
            sb_add_int(&out, e->coluna); sb_add(&out, ": "); sb_add(&out, e->erro); sb_addc(&out, ' ');
            repr_str(&out, e->lexema, e->lexema_len);
        }
        for (int i = 0; i < parser.erros.tamanho; i++) {
            ErroSint *e = &parser.erros.dados[i];
            LINHA_INI();
            sb_add(&out, "ERRO SINTÁTICO "); sb_add_int(&out, e->linha); sb_addc(&out, ':');
            sb_add_int(&out, e->coluna); sb_add(&out, ": "); sb_add(&out, e->mensagem);
        }
        LINHA_INI();
        sb_add(&out, "Análise semântica não executada (entrada rejeitada pelo scanner/parser).");
        fwrite(out.dados, 1, out.tamanho, stdout);
        return 2;
    }

    PV *ast = conv(ast_c);
    analisar(ast);

    for (int i = 0; i < ndiags; i++) {
        LINHA_INI();
        sb_add(&out, diags[i].codigo); sb_add(&out, " — linha "); sb_add_int(&out, diags[i].linha);
        sb_add(&out, ", coluna "); sb_add_int(&out, diags[i].coluna); sb_add(&out, ": ");
        sb_add_n(&out, diags[i].msg.p, diags[i].msg.n);
    }
    if (flag_ast) {
        LINHA_INI();
        pv_json(&out, ast, 0);
    }
    LINHA_INI();
    if (ndiags == 0) sb_add(&out, "Análise semântica concluída: 0 erros; programa aceito.");
    else {
        sb_add(&out, "Análise semântica concluída: "); sb_add_int(&out, ndiags);
        sb_add(&out, ndiags == 1 ? " erro" : " erros"); sb_add(&out, "; programa rejeitado.");
    }
    fwrite(out.dados, 1, out.tamanho, stdout);
    return ndiags == 0 ? 0 : 1;
}
#endif
