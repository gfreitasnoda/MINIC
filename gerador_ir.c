#define MINIC_NO_ENTRY
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "analisador_semantico.c"
#pragma GCC diagnostic pop

//Recuo base das linhas da saida
#define GI_BASE ""

typedef struct { Simbolo *sym; const char *nome; } GiRenome;
typedef struct { Str cont, brk; int usou_continue; } GiLaco;

typedef struct {
    StrBuf linhas;
    int nlinhas;
    const char *indent;
    int n_temp;
    const char **rotulos; int nrot, caprot;
    int usou_rotulos, usou_vetor, usou_logico, usou_sombra;
    GiLaco *lacos; int nlacos, caplacos;
    GiRenome *renome; int nren, capren;
    PV **init_feito; int ninit, capinit;
} GeradorIR;

static const Str GI_NADA = { NULL, 0 };

static Str gi_visitar(GeradorIR *g, PV *no);
static void gi_visitar_itens(GeradorIR *g, PV **itens, int n);

//Infra
static void gi_emitir(GeradorIR *g, Str texto) {
    sb_add(&g->linhas, g->indent);
    sb_add_n(&g->linhas, texto.p, texto.n);
    sb_addc(&g->linhas, '\n');
    g->nlinhas++;
}

static void gi_linha_base(GeradorIR *g, Str texto) {
    sb_add(&g->linhas, GI_BASE);
    sb_add_n(&g->linhas, texto.p, texto.n);
    sb_addc(&g->linhas, '\n');
    g->nlinhas++;
}

static void gi_rotulo_def(GeradorIR *g, const char *rotulo) {
    gi_linha_base(g, mk("%s:", rotulo));
}

static Str gi_novo_temp(GeradorIR *g) {
    g->n_temp++;
    return mk("t%d", g->n_temp);
}

static int gi_rotulo_usado(GeradorIR *g, const char *nome) {
    for (int i = 0; i < g->nrot; i++) if (strcmp(g->rotulos[i], nome) == 0) return 1;
    return 0;
}

//Rotulo unico na funcao; fixo nao conta como rotulo variavel
static const char *gi_novo_rotulo(GeradorIR *g, const char *base, int fixo) {
    if (!fixo) g->usou_rotulos = 1;
    const char *nome = base;
    int n = 1;
    while (gi_rotulo_usado(g, nome)) {
        n++;
        Str s = mk("%s_%d", base, n);
        nome = s.p;
    }
    if (g->nrot == g->caprot) {
        g->caprot = g->caprot ? g->caprot * 2 : 8;
        g->rotulos = xrealloc((void *)g->rotulos, (size_t)g->caprot * sizeof(char *));
    }
    g->rotulos[g->nrot++] = nome;
    return nome;
}

//Nomes e sombreamento
static Simbolo *gi_simbolo(PV *no) {
    PV *s = pv_get(no, "sem");
    if (!s || s->k != PV_DICT) return NULL;
    PV *y = pv_get(s, "simbolo");
    return (y && y->k == PV_SYM) ? y->sym : NULL;
}

static const char *gi_nome(GeradorIR *g, PV *no) {
    Simbolo *sym = gi_simbolo(no);
    if (sym) {
        for (int i = 0; i < g->nren; i++) if (g->renome[i].sym == sym) return g->renome[i].nome;
    }
    return GS(no, "nome");
}

static void gi_achar(PV *n, PV ***arr, int *cnt, int *cap) {
    if (!n) return;
    if (n->k == PV_DICT) {
        PV *k = pv_get(n, "node");
        if (k && k->k == PV_STR && (E(k->s, "Param") || E(k->s, "VarDecl"))) {
            if (*cnt == *cap) {
                *cap = *cap ? *cap * 2 : 16;
                *arr = xrealloc(*arr, (size_t)*cap * sizeof(PV *));
            }
            (*arr)[(*cnt)++] = n;
        }
        for (int i = 0; i < n->n; i++)
            if (n->v[i]->k == PV_DICT || n->v[i]->k == PV_LIST) gi_achar(n->v[i], arr, cnt, cap);
    } else if (n->k == PV_LIST) {
        for (int i = 0; i < n->n; i++)
            if (n->v[i]->k == PV_DICT || n->v[i]->k == PV_LIST) gi_achar(n->v[i], arr, cnt, cap);
    }
}

typedef struct { const char *nome; Simbolo **syms; int n; } GiPorNome;

//Renomeia simbolos homonimos
static void gi_preparar_nomes(GeradorIR *g, PV *func) {
    PV **decls = NULL; int nd = 0, cd = 0;
    gi_achar(G(func, "params"), &decls, &nd, &cd);
    gi_achar(G(func, "corpo"), &decls, &nd, &cd);
    GiPorNome *por = NULL; int np = 0;
    if (nd) por = xmalloc((size_t)nd * sizeof(GiPorNome));
    for (int i = 0; i < nd; i++) {
        Simbolo *sym = gi_simbolo(decls[i]);
        if (!sym) continue;
        const char *nome = GS(decls[i], "nome");
        int e = -1;
        for (int j = 0; j < np; j++) if (strcmp(por[j].nome, nome) == 0) { e = j; break; }
        if (e < 0) {
            e = np++;
            por[e].nome = nome;
            por[e].syms = xmalloc((size_t)nd * sizeof(Simbolo *));
            por[e].n = 0;
        }
        int ja = 0;
        for (int j = 0; j < por[e].n; j++) if (por[e].syms[j] == sym) ja = 1;
        if (!ja) por[e].syms[por[e].n++] = sym;
    }
    g->nren = 0;
    for (int e = 0; e < np; e++) {
        if (por[e].n <= 1) continue;
        g->usou_sombra = 1;
        for (int j = 0; j < por[e].n; j++) {
            Str nm;
            if (por[e].n == 2) nm = mk("%s#%s", por[e].nome, j == 0 ? "outer" : "inner");
            else nm = mk("%s#%d", por[e].nome, j + 1);
            if (g->nren == g->capren) {
                g->capren = g->capren ? g->capren * 2 : 8;
                g->renome = xrealloc(g->renome, (size_t)g->capren * sizeof(GiRenome));
            }
            g->renome[g->nren].sym = por[e].syms[j];
            g->renome[g->nren].nome = nm.p;
            g->nren++;
        }
    }
    for (int e = 0; e < np; e++) free(por[e].syms);
    free(por);
    free(decls);
}

//Programa e funcoes
static void gi_visit_function(GeradorIR *g, PV *no) {
    g->n_temp = 0;
    g->nrot = 0;
    gi_preparar_nomes(g, no);
    StrBuf ps = {0};
    PV *params = G(no, "params");
    for (int i = 0; i < params->n; i++) {
        if (i) sb_add(&ps, ", ");
        sb_add(&ps, gi_nome(g, params->v[i]));
    }
    if (ps.tamanho) gi_linha_base(g, mk("func %s(%s)", GS(no, "nome"), ps.dados));
    else gi_linha_base(g, mk("func %s", GS(no, "nome")));
    free(ps.dados);
    g->indent = GI_BASE "  ";
    PV *itens = G(G(no, "corpo"), "itens");
    gi_visitar_itens(g, itens->v, itens->n);
    g->indent = GI_BASE;
    gi_linha_base(g, mk("endfunc"));
}

//Comandos
static int gi_termina_em_return(PV *no) {
    const char *k = K(no);
    if (E(k, "Return")) return 1;
    if (E(k, "Block")) {
        PV *itens = G(no, "itens");
        return itens->n > 0 && E(K(itens->v[itens->n - 1]), "Return");
    }
    return 0;
}

static int gi_eh_guarda_com_retorno(PV *item, int resto) {
    return E(K(item), "If") && is_none(G(item, "senao")) && resto > 0
           && gi_termina_em_return(G(item, "entao"));
}

static void gi_gerar_guarda(GeradorIR *g, PV *no, PV **resto, int n) {
    Str c = gi_visitar(g, G(no, "condicao"));
    const char *l_rec = gi_novo_rotulo(g, "L_rec", 1);
    const char *l_fim = gi_novo_rotulo(g, "L_end", 1);
    gi_emitir(g, mk("ifFalse %S goto %s", c, l_rec));
    gi_visitar(g, G(no, "entao"));
    gi_emitir(g, mk("goto %s", l_fim));
    gi_rotulo_def(g, l_rec);
    gi_visitar_itens(g, resto, n);
    gi_rotulo_def(g, l_fim);
}

static int gi_usa_nome(PV *no, const char *nome) {
    if (!no) return 0;
    if (no->k == PV_DICT) {
        PV *n = pv_get(no, "nome");
        if (n && n->k == PV_STR && strcmp(n->s, nome) == 0) return 1;
        for (int i = 0; i < no->n; i++)
            if ((no->v[i]->k == PV_DICT || no->v[i]->k == PV_LIST) && gi_usa_nome(no->v[i], nome)) return 1;
        return 0;
    }
    if (no->k == PV_LIST) {
        for (int i = 0; i < no->n; i++) if (gi_usa_nome(no->v[i], nome)) return 1;
    }
    return 0;
}

//Antecipa a inicializacao do for
static void gi_antecipar_init_for(GeradorIR *g, PV *decl, PV **resto, int n) {
    if (!is_none(G(decl, "valor")) || !is_none(G(decl, "tamanho"))) return;
    const char *nome = GS(decl, "nome");
    for (int j = 0; j < n; j++) {
        PV *item = resto[j];
        if (E(K(item), "For")) {
            PV *init = G(item, "init");
            if (!is_none(init) && E(K(init), "ExprStmt")) init = G(init, "expressao");
            if (!is_none(init) && E(K(init), "Assign")
                && E(K(G(init, "alvo")), "Id")
                && strcmp(GS(G(init, "alvo"), "nome"), nome) == 0
                && E(K(G(init, "valor")), "Lit")) {
                gi_visitar(g, init);
                if (g->ninit == g->capinit) {
                    g->capinit = g->capinit ? g->capinit * 2 : 8;
                    g->init_feito = xrealloc(g->init_feito, (size_t)g->capinit * sizeof(PV *));
                }
                g->init_feito[g->ninit++] = item;
            }
            return;
        }
        if (gi_usa_nome(item, nome)) return;
    }
}

//Visita os comandos em ordem
static void gi_visitar_itens(GeradorIR *g, PV **itens, int n) {
    for (int k = 0; k < n; k++) {
        PV *item = itens[k];
        if (E(K(item), "VarDecl")) {
            gi_antecipar_init_for(g, item, &itens[k + 1], n - k - 1);
        } else if (gi_eh_guarda_com_retorno(item, n - k - 1)) {
            gi_gerar_guarda(g, item, &itens[k + 1], n - k - 1);
            return;
        }
        gi_visitar(g, item);
    }
}

static void gi_visit_vardecl(GeradorIR *g, PV *no) {
    if (!is_none(G(no, "tamanho"))) return;    //vetor nao gera instrucao
    if (!is_none(G(no, "valor"))) {
        Str v = gi_visitar(g, G(no, "valor"));
        gi_emitir(g, mk("%s = %S", gi_nome(g, no), v));
    }
}

static int gi_retorno_void(PV *no) {
    PV *s = pv_get(no, "sem");
    if (!s || s->k != PV_DICT) return 0;
    PV *t = pv_get(s, "tipo_retorno");
    return t && t->k == PV_STR && strcmp(t->s, "void") == 0;
}

static Str gi_gerar_chamada(GeradorIR *g, PV *no, int com_resultado) {
    PV *args = G(no, "argumentos");
    Str *ops = NULL;
    if (args->n) ops = xmalloc((size_t)args->n * sizeof(Str));
    for (int i = 0; i < args->n; i++) ops[i] = gi_visitar(g, args->v[i]);
    for (int i = 0; i < args->n; i++) gi_emitir(g, mk("param %S", ops[i]));
    Str chamada = mk("call %s, %d", GS(G(no, "callee"), "nome"), args->n);
    free(ops);
    if (!com_resultado) {
        gi_emitir(g, chamada);
        return GI_NADA;
    }
    Str t = gi_novo_temp(g);
    gi_emitir(g, mk("%S = %S", t, chamada));
    return t;
}

//Chamada void usada como comando nao precisa de temporario
static void gi_expr_como_comando(GeradorIR *g, PV *expr) {
    if (E(K(expr), "Call") && gi_retorno_void(expr)) gi_gerar_chamada(g, expr, 0);
    else gi_visitar(g, expr);
}

static void gi_visit_if(GeradorIR *g, PV *no) {
    Str c = gi_visitar(g, G(no, "condicao"));
    int tem_else = !is_none(G(no, "senao"));
    const char *l_else = gi_novo_rotulo(g, tem_else ? "L_else" : "L_end_if", 0);
    gi_emitir(g, mk("ifFalse %S goto %s", c, l_else));
    gi_visitar(g, G(no, "entao"));
    if (tem_else) {
        const char *l_fim = gi_novo_rotulo(g, "L_end", 0);
        gi_emitir(g, mk("goto %s", l_fim));
        gi_rotulo_def(g, l_else);
        gi_visitar(g, G(no, "senao"));
        gi_rotulo_def(g, l_fim);
    } else {
        gi_rotulo_def(g, l_else);
    }
}

static void gi_empilhar_laco(GeradorIR *g, const char *cont, const char *brk) {
    if (g->nlacos == g->caplacos) {
        g->caplacos = g->caplacos ? g->caplacos * 2 : 8;
        g->lacos = xrealloc(g->lacos, (size_t)g->caplacos * sizeof(GiLaco));
    }
    g->lacos[g->nlacos].cont = mk("%s", cont);
    g->lacos[g->nlacos].brk = mk("%s", brk);
    g->lacos[g->nlacos].usou_continue = 0;
    g->nlacos++;
}

static void gi_visit_while(GeradorIR *g, PV *no) {
    const char *l_ini = gi_novo_rotulo(g, "L_test", 0);
    const char *l_fim = gi_novo_rotulo(g, "L_end", 0);
    gi_rotulo_def(g, l_ini);
    Str c = gi_visitar(g, G(no, "condicao"));
    gi_emitir(g, mk("ifFalse %S goto %s", c, l_fim));
    gi_empilhar_laco(g, l_ini, l_fim);
    gi_visitar(g, G(no, "corpo"));
    g->nlacos--;
    gi_emitir(g, mk("goto %s", l_ini));
    gi_rotulo_def(g, l_fim);
}

static int gi_init_feito(GeradorIR *g, PV *no) {
    for (int i = 0; i < g->ninit; i++) if (g->init_feito[i] == no) return 1;
    return 0;
}

static void gi_visit_for(GeradorIR *g, PV *no) {
    const char *l_cond = gi_novo_rotulo(g, "L_test", 0);
    const char *l_inc = gi_novo_rotulo(g, "L_inc", 0);
    const char *l_fim = gi_novo_rotulo(g, "L_end", 0);
    if (!is_none(G(no, "init")) && !gi_init_feito(g, no)) gi_visitar(g, G(no, "init"));
    gi_rotulo_def(g, l_cond);
    if (!is_none(G(no, "condicao"))) {
        Str c = gi_visitar(g, G(no, "condicao"));
        gi_emitir(g, mk("ifFalse %S goto %s", c, l_fim));
    }
    gi_empilhar_laco(g, l_inc, l_fim);    //continue vai ao incremento
    gi_visitar(g, G(no, "corpo"));
    int usou_continue = g->lacos[--g->nlacos].usou_continue;
    if (usou_continue) gi_rotulo_def(g, l_inc);
    if (!is_none(G(no, "incremento"))) gi_expr_como_comando(g, G(no, "incremento"));
    gi_emitir(g, mk("goto %s", l_cond));
    gi_rotulo_def(g, l_fim);
}

//Expressoes
static Str gi_visit_lit(PV *no) {
    const char *t = GS(no, "tipo");
    PV *v = G(no, "valor");
    if (E(t, "bool")) return mk("%s", v->b ? "true" : "false");
    if (E(t, "string")) {
        StrBuf sb = {0};
        json_str(&sb, v->s, v->sn);
        return str_from_sb(&sb);
    }
    if (E(t, "char")) {
        StrBuf sb = {0};
        sb_addc(&sb, '\'');
        for (size_t i = 0; i < v->sn; i++) {
            if (v->s[i] == '\'') sb_add(&sb, "\\'");
            else sb_addc(&sb, v->s[i]);
        }
        sb_addc(&sb, '\'');
        return str_from_sb(&sb);
    }
    return pv_fstr(v);
}

static const char *gi_op_ir(const char *op) {
    if (E(op, "&&")) return "and";
    if (E(op, "||")) return "or";
    return op;
}

static Str gi_visit_binary(GeradorIR *g, PV *no) {
    Str e = gi_visitar(g, G(no, "esquerda"));    //esquerda antes da direita
    Str d = gi_visitar(g, G(no, "direita"));
    Str t = gi_novo_temp(g);
    const char *op = gi_op_ir(GS(no, "op"));
    if (E(op, "and") || E(op, "or")) g->usou_logico = 1;
    gi_emitir(g, mk("%S = %S %s %S", t, e, op, d));
    return t;
}

static Str gi_visit_unary(GeradorIR *g, PV *no) {
    Str o = gi_visitar(g, G(no, "operando"));
    Str t = gi_novo_temp(g);
    if (E(GS(no, "op"), "!")) {
        g->usou_logico = 1;
        gi_emitir(g, mk("%S = not %S", t, o));
    } else {
        gi_emitir(g, mk("%S = %s%S", t, GS(no, "op"), o));
    }
    return t;
}

//Tamanho do elemento
#define GI_ESCALA 1

//Calcula o deslocamento do indice e devolve vetor e temporario
static Str gi_endereco(GeradorIR *g, PV *idx_no, const char **base) {
    g->usou_vetor = 1;
    *base = gi_nome(g, G(idx_no, "base"));
    Str idx = gi_visitar(g, G(idx_no, "indice"));
    Str t = gi_novo_temp(g);
    gi_emitir(g, mk("%S = %S * %d", t, idx, GI_ESCALA));
    return t;
}

static Str gi_visit_index(GeradorIR *g, PV *no) {
    const char *base;
    Str ender = gi_endereco(g, no, &base);
    Str t = gi_novo_temp(g);
    gi_emitir(g, mk("%S = load %s[%S]", t, base, ender));
    return t;
}

static Str gi_visit_assign(GeradorIR *g, PV *no) {
    PV *alvo = G(no, "alvo");
    if (E(K(alvo), "Index")) {    //indice avaliado antes do valor
        const char *base;
        Str ender = gi_endereco(g, alvo, &base);
        Str v = gi_visitar(g, G(no, "valor"));
        gi_emitir(g, mk("store %s[%S], %S", base, ender, v));
        return v;
    }
    Str v = gi_visitar(g, G(no, "valor"));
    const char *destino = gi_nome(g, alvo);
    gi_emitir(g, mk("%s = %S", destino, v));
    return mk("%s", destino);    //atribuicao encadeada
}

//Despacho por tipo de no
static Str gi_visitar(GeradorIR *g, PV *no) {
    const char *k = K(no);
    if (E(k, "Program")) {
        PV *ds = G(no, "declaracoes");
        for (int i = 0; i < ds->n; i++) gi_visitar(g, ds->v[i]);
        return GI_NADA;
    }
    if (E(k, "Function")) { gi_visit_function(g, no); return GI_NADA; }
    if (E(k, "Block")) {
        PV *itens = G(no, "itens");
        gi_visitar_itens(g, itens->v, itens->n);
        return GI_NADA;
    }
    if (E(k, "VarDecl")) { gi_visit_vardecl(g, no); return GI_NADA; }
    if (E(k, "ExprStmt")) {
        if (!is_none(G(no, "expressao"))) gi_expr_como_comando(g, G(no, "expressao"));
        return GI_NADA;
    }
    if (E(k, "Print")) {
        Str v = gi_visitar(g, G(no, "valor"));
        gi_emitir(g, mk("print %S", v));
        return GI_NADA;
    }
    if (E(k, "Read")) {
        PV *alvo = G(no, "alvo");
        if (E(K(alvo), "Index")) {
            const char *base;
            Str ender = gi_endereco(g, alvo, &base);
            gi_emitir(g, mk("read %s[%S]", base, ender));
        } else {
            gi_emitir(g, mk("read %s", gi_nome(g, alvo)));
        }
        return GI_NADA;
    }
    if (E(k, "Return")) {
        if (is_none(G(no, "valor"))) {
            gi_emitir(g, mk("return"));
        } else {
            Str v = gi_visitar(g, G(no, "valor"));
            gi_emitir(g, mk("return %S", v));
        }
        return GI_NADA;
    }
    if (E(k, "If")) { gi_visit_if(g, no); return GI_NADA; }
    if (E(k, "While")) { gi_visit_while(g, no); return GI_NADA; }
    if (E(k, "For")) { gi_visit_for(g, no); return GI_NADA; }
    if (E(k, "Break")) {
        gi_emitir(g, mk("goto %S", g->lacos[g->nlacos - 1].brk));
        return GI_NADA;
    }
    if (E(k, "Continue")) {
        g->lacos[g->nlacos - 1].usou_continue = 1;
        gi_emitir(g, mk("goto %S", g->lacos[g->nlacos - 1].cont));
        return GI_NADA;
    }
    if (E(k, "Lit")) return gi_visit_lit(no);
    if (E(k, "Id")) return mk("%s", gi_nome(g, no));
    if (E(k, "Binary")) return gi_visit_binary(g, no);
    if (E(k, "Unary")) return gi_visit_unary(g, no);
    if (E(k, "Index")) return gi_visit_index(g, no);
    if (E(k, "Assign")) return gi_visit_assign(g, no);
    if (E(k, "Call")) return gi_gerar_chamada(g, no, 1);
    fprintf(stderr, "IR: no sem visitor: %s\n", k);
    exit(1);
}

//Gera a IR (texto) a partir da AST
static Str gi_gerar(GeradorIR *g, PV *ast) {
    memset(&g->linhas, 0, sizeof g->linhas);
    g->nlinhas = 0;
    g->indent = GI_BASE;
    g->usou_rotulos = g->usou_vetor = g->usou_logico = g->usou_sombra = 0;
    gi_visitar(g, ast);
    if (g->nlinhas == 0) return mk("\n");
    return str_from_sb(&g->linhas);
}
