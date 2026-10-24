#include "gerador_ir.c"
#include "otimizador_ir.c"

#define MC_EXIT_OK 0
#define MC_EXIT_IO 1
#define MC_EXIT_LEXICO 2
#define MC_EXIT_SINTATICO 3
#define MC_EXIT_SEMANTICO 4

#define MC_OBS_VETOR "Observação: o fator de escala do endereço depende da representação " \
    "adotada; deve haver cálculo de endereço e operações de leitura/escrita " \
    "do vetor.\n"
#define MC_OBS_SOMBRA "Observação: nomes temporários/endereços de símbolos podem substituir " \
    "a notação #outer/#inner.\n"

//Letra base ASCII minuscula das letras latinas U+00C0 a U+017F (0 se nao ha)
static const unsigned char MC_BASE[192] = {
    97, 97, 97, 97, 97, 97, 0, 99, 101, 101, 101, 101, 105, 105, 105, 105,
    0, 110, 111, 111, 111, 111, 111, 0, 0, 117, 117, 117, 117, 121, 0, 0,
    97, 97, 97, 97, 97, 97, 0, 99, 101, 101, 101, 101, 105, 105, 105, 105,
    0, 110, 111, 111, 111, 111, 111, 0, 0, 117, 117, 117, 117, 121, 0, 121,
    97, 97, 97, 97, 97, 97, 99, 99, 99, 99, 99, 99, 99, 99, 100, 100,
    0, 0, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 103, 103, 103, 103,
    103, 103, 103, 103, 104, 104, 0, 0, 105, 105, 105, 105, 105, 105, 105, 105,
    105, 0, 0, 0, 106, 106, 107, 107, 0, 108, 108, 108, 108, 108, 108, 0,
    0, 0, 0, 110, 110, 110, 110, 110, 110, 0, 0, 0, 111, 111, 111, 111,
    111, 111, 0, 0, 114, 114, 114, 114, 114, 114, 115, 115, 115, 115, 115, 115,
    115, 115, 116, 116, 116, 116, 0, 0, 117, 117, 117, 117, 117, 117, 117, 117,
    117, 117, 117, 117, 119, 119, 121, 121, 121, 122, 122, 122, 122, 122, 122, 0,
};

//Texto
static unsigned mc_lower_cp(unsigned cp) {
    if (cp >= 'A' && cp <= 'Z') return cp + 32;
    if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) return cp + 32;
    return cp;
}

static int mc_word_cp(unsigned cp) {
    if (cp < 0x80) return (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_';
    if (cp == 0xAA || cp == 0xB5 || cp == 0xBA || cp == 0xB2 || cp == 0xB3 || cp == 0xB9) return 1;
    if (cp < 0xC0 || cp == 0xD7 || cp == 0xF7) return 0;
    if (cp >= 0x2000 && cp < 0x2C00) return 0;
    if (cp >= 0x3000 && cp < 0x3040) return 0;
    if (cp >= 0xE000 && cp < 0xF900) return 0;
    if (cp >= 0x1F000 && cp < 0x20000) return 0;
    return 1;
}

static int mc_alnum_cp(unsigned cp) { return cp != '_' && mc_word_cp(cp); }

//Minusculas e sem acentos, so para reconhecer palavras-chave
static char *mc_norm(Str s) {
    StrBuf sb = {0};
    size_t i = 0;
    while (i < s.n) {
        int L;
        unsigned cp = cp_decode(s.p, i, s.n, &L);
        i += (size_t)L;
        if (cp < 0x80) sb_addc(&sb, (char)mc_lower_cp(cp));
        else if (cp >= 0xC0 && cp < 0x180 && MC_BASE[cp - 0xC0]) sb_addc(&sb, (char)MC_BASE[cp - 0xC0]);
    }
    if (!sb.dados) return xstrdup("");
    return sb.dados;
}

//Trechos entre aspas simples, duplas ou tipograficas
typedef struct { Str *v; int n, cap; } McTrechos;

static int mc_ini_aspas(unsigned cp) { return cp == 0x201C || cp == '"' || cp == '\'' || cp == '`' || cp == 0x2018; }
static int mc_fim_aspas(unsigned cp) { return cp == 0x201D || cp == '"' || cp == '\'' || cp == '`' || cp == 0x2019; }
static int mc_excl_aspas(unsigned cp) { return mc_ini_aspas(cp) || mc_fim_aspas(cp); }

static McTrechos mc_citados(Str s) {
    McTrechos r = {0};
    size_t i = 0;
    while (i < s.n) {
        int L;
        unsigned cp = cp_decode(s.p, i, s.n, &L);
        if (!mc_ini_aspas(cp)) { i += (size_t)L; continue; }
        size_t j = i + (size_t)L, ini = j;
        int cnt = 0;
        while (j < s.n) {
            int L2;
            unsigned c2 = cp_decode(s.p, j, s.n, &L2);
            if (mc_excl_aspas(c2)) break;
            j += (size_t)L2;
            cnt++;
        }
        int ok = 0, L3 = 0;
        if (cnt > 0 && j < s.n) {
            unsigned c3 = cp_decode(s.p, j, s.n, &L3);
            ok = mc_fim_aspas(c3);
        }
        if (!ok) { i += (size_t)L; continue; }
        if (r.n == r.cap) {
            r.cap = r.cap ? r.cap * 2 : 4;
            r.v = xrealloc(r.v, (size_t)r.cap * sizeof(Str));
        }
        r.v[r.n].p = xmemdup(s.p + ini, j - ini);
        r.v[r.n].n = j - ini;
        r.n++;
        i = j + (size_t)L3;
    }
    return r;
}

//Casa a palavra (UTF-8) sem diferenciar maiusculas a partir de i
static int mc_casa_palavra(Str s, size_t i, const char *kw, size_t *fim) {
    size_t kn = strlen(kw), k = 0;
    while (k < kn) {
        if (i >= s.n) return 0;
        int L1, L2;
        unsigned a = cp_decode(s.p, i, s.n, &L1), b = cp_decode(kw, k, kn, &L2);
        if (mc_lower_cp(a) != mc_lower_cp(b)) return 0;
        i += (size_t)L1;
        k += (size_t)L2;
    }
    *fim = i;
    return 1;
}

//Nome citado na mensagem ou a palavra apos identificador/variavel/simbolo
static Str mc_nome_da_msg(Str msg) {
    McTrechos q = mc_citados(msg);
    if (q.n) return q.v[0];
    static const char *const kws[] = { "identificador", "variável", "variavel", "símbolo", "simbolo" };
    for (size_t i = 0; i < msg.n;) {
        int L;
        cp_decode(msg.p, i, msg.n, &L);
        for (size_t k = 0; k < sizeof kws / sizeof kws[0]; k++) {
            size_t j;
            if (!mc_casa_palavra(msg, i, kws[k], &j)) continue;
            size_t e = j;
            int esp = 0;
            while (e < msg.n) {
                int L2;
                unsigned c = cp_decode(msg.p, e, msg.n, &L2);
                if (!espaco_cp(c)) break;
                e += (size_t)L2;
                esp++;
            }
            if (!esp) continue;
            size_t ini = e;
            while (e < msg.n) {
                int L2;
                unsigned c = cp_decode(msg.p, e, msg.n, &L2);
                if (!mc_word_cp(c)) break;
                e += (size_t)L2;
            }
            if (e > ini) { Str r; r.p = xmemdup(msg.p + ini, e - ini); r.n = e - ini; return r; }
        }
        i += (size_t)L;
    }
    return mk("?");
}

//Percorre a AST devolvendo todos os nos-dicionario em pre-ordem
typedef struct { PV **v; int n, cap; } McNos;

static void mc_nos(PV *no, McNos *r) {
    if (!no) return;
    if (no->k == PV_DICT) {
        if (r->n == r->cap) {
            r->cap = r->cap ? r->cap * 2 : 64;
            r->v = xrealloc(r->v, (size_t)r->cap * sizeof(PV *));
        }
        r->v[r->n++] = no;
        for (int i = 0; i < no->n; i++)
            if (no->v[i]->k == PV_DICT || no->v[i]->k == PV_LIST) mc_nos(no->v[i], r);
    } else if (no->k == PV_LIST) {
        for (int i = 0; i < no->n; i++) mc_nos(no->v[i], r);
    }
}

static const char *mc_node(PV *d) {
    PV *k = pv_get(d, "node");
    return (k && k->k == PV_STR) ? k->s : NULL;
}

static int mc_eh(PV *d, const char *kind) {
    const char *k = mc_node(d);
    return k && strcmp(k, kind) == 0;
}

static const char *mc_str_de(PV *d, const char *chave) {
    PV *v = pv_get(d, chave);
    return (v && v->k == PV_STR) ? v->s : NULL;
}

//Tipo de uma expressao: literal ou anotado pelo semantico
static const char *mc_tipo_de(PV *expr) {
    if (!expr || expr->k != PV_DICT) return NULL;
    if (mc_eh(expr, "Lit")) return mc_str_de(expr, "tipo");
    PV *s = pv_get(expr, "sem");
    if (!s || s->k != PV_DICT) return NULL;
    return mc_str_de(s, "tipo");
}

static int mc_tem(const char *n, const char *sub) { return strstr(n, sub) != NULL; }

//Texto do criterio para o erro semantico
static Str mc_criterio_semantico(Str msg, PV *ast) {
    char *n = mc_norm(msg);
    McNos nos = {0};
    mc_nos(ast, &nos);
    if (mc_tem(n, "break") || mc_tem(n, "continue")) {
        const char *cmd = mc_tem(n, "continue") ? "continue" : "break";
        return mk("%s só pode ocorrer dentro de while/for.", cmd);
    }
    if (mc_tem(n, "indice") || mc_tem(n, "index")) return mk("índice do vetor deve ter tipo int.");
    if (mc_tem(n, "retorn") || mc_tem(n, "return")) {
        for (int a = 0; a < nos.n; a++) {
            PV *f = nos.v[a];
            if (!mc_eh(f, "Function")) continue;
            McNos sub = {0};
            mc_nos(pv_get(f, "corpo"), &sub);
            for (int b = 0; b < sub.n; b++) {
                PV *r = sub.v[b];
                const char *t = mc_eh(r, "Return") ? mc_tipo_de(pv_get(r, "valor")) : NULL;
                const char *ft = mc_str_de(f, "tipo");
                if (t && *t && (!ft || strcmp(t, ft) != 0))
                    return mk("retorno %s incompatível com o tipo %s declarado para %s.", t, ft, mc_str_de(f, "nome"));
            }
        }
    }
    if (mc_tem(n, "argument") || mc_tem(n, "parametr")) {
        for (int a = 0; a < nos.n; a++) {
            PV *c = nos.v[a];
            if (!mc_eh(c, "Call")) continue;
            const char *nome = mc_str_de(pv_get(c, "callee"), "nome");
            if (!nome) continue;
            PV *f = NULL;
            for (int b = 0; b < nos.n; b++)
                if (mc_eh(nos.v[b], "Function") && mc_str_de(nos.v[b], "nome") && strcmp(mc_str_de(nos.v[b], "nome"), nome) == 0)
                    f = nos.v[b];
            if (!f) continue;
            int na = pv_get(c, "argumentos")->n, np = pv_get(f, "params")->n;
            if (na != np)
                return mk("chamada de %s requer %d argumentos e recebeu %d.", mc_str_de(f, "nome"), np, na);
        }
    }
    if (mc_tem(n, "duplic") || mc_tem(n, "redecl") || mc_tem(n, "ja declar"))
        return mk("diagnóstico de declaração duplicada de “%S” no mesmo escopo.", mc_nome_da_msg(msg));
    if ((mc_tem(n, "declarad") || mc_tem(n, "definid") || mc_tem(n, "desconhecid"))
        && (mc_tem(n, "nao") || mc_tem(n, "undeclared") || mc_tem(n, "undefined") || mc_tem(n, "desconhecid")))
        return mk("diagnóstico de identificador “%S” não declarado; "
                  "não emitir IR executável como se a compilação tivesse sucesso.", mc_nome_da_msg(msg));
    if ((mc_tem(n, "operand") || mc_tem(n, "operador")) && mc_tem(n, "bool")) {
        McTrechos q = mc_citados(msg);
        Str op = {NULL, 0};
        for (int i = 0; i < q.n && !op.p; i++) {
            Str x = q.v[i];
            if (x.n == 1 && strchr("+-*/%", x.p[0])) op = x;
        }
        if (!op.p) {
            for (int a = 0; a < nos.n && !op.p; a++) {
                if (mc_eh(nos.v[a], "Binary") && mc_str_de(nos.v[a], "op")) op = mk("%s", mc_str_de(nos.v[a], "op"));
            }
        }
        return mk("operandos de “%S” devem ser numéricos compatíveis; "
                  "bool não pode ser usado como int implicitamente.", op.p ? op : mk("?"));
    }
    if (mc_tem(n, "bool") && mc_tem(n, "int"))
        return mk("rejeitar conversão implícita bool -> int na versão estrita da especificação.");
    return msg;
}

//Linhas do codigo separadas como str.splitlines()
typedef struct { size_t ini, n; } McLinha;

static McLinha *mc_linhas(const char *s, size_t len, int *cnt) {
    McLinha *v = xmalloc((len + 2) * sizeof(McLinha));
    int c = 0;
    size_t ini = 0, i = 0;
    while (i < len) {
        unsigned char ch = (unsigned char)s[i];
        size_t sep = 0;
        if (ch == '\n' || ch == '\r' || ch == 0x0b || ch == 0x0c || (ch >= 0x1c && ch <= 0x1e)) {
            sep = 1;
            if (ch == '\r' && i + 1 < len && s[i + 1] == '\n') sep = 2;
        } else if (ch == 0xC2 && i + 1 < len && (unsigned char)s[i + 1] == 0x85) sep = 2;
        else if (ch == 0xE2 && i + 2 < len && (unsigned char)s[i + 1] == 0x80
                 && ((unsigned char)s[i + 2] == 0xA8 || (unsigned char)s[i + 2] == 0xA9)) sep = 3;
        if (sep) {
            v[c].ini = ini; v[c].n = i - ini; c++;
            i += sep;
            ini = i;
        } else {
            i++;
        }
    }
    if (ini < len) { v[c].ini = ini; v[c].n = len - ini; c++; }
    *cnt = c;
    return v;
}

//Linha comeca (apos espacos) por tipo seguido de fronteira de palavra
static int mc_comeca_com_tipo(const char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        int L;
        unsigned c = cp_decode(s, i, n, &L);
        if (!espaco_cp(c)) break;
        i += (size_t)L;
    }
    static const char *const tipos[] = { "int", "float", "bool", "char", "string" };
    for (size_t k = 0; k < sizeof tipos / sizeof tipos[0]; k++) {
        size_t kl = strlen(tipos[k]);
        if (i + kl > n || strncmp(s + i, tipos[k], kl) != 0) continue;
        if (i + kl == n) return 1;
        int L;
        unsigned c = cp_decode(s, i + kl, n, &L);
        if (!mc_word_cp(c)) return 1;
    }
    return 0;
}

static Str mc_criterio_sintatico(const ErroSint *e, const char *codigo) {
    Str msg;
    msg.p = e->mensagem;
    msg.n = e->mensagem_len;
    char *n = mc_norm(msg);
    if (memchr(msg.p, ';', msg.n) || mc_tem(n, "ponto e virgula") || mc_tem(n, "ponto-e-virgula")) {
        int cnt;
        McLinha *ls = mc_linhas(codigo, strlen(codigo), &cnt);
        int i = e->linha - 1;
        int de = i - 1 > 0 ? i - 1 : 0;
        for (int k = de; k <= i && k < cnt; k++)
            if (mc_comeca_com_tipo(codigo + ls[k].ini, ls[k].n)) return mk("diagnóstico de “;” esperado após declaração local.");
        return mk("diagnóstico de “;” esperado.");
    }
    return msg;
}

static Str mc_criterio_lexico(const Erro *e) {
    size_t cps = 0;
    unsigned primeiro = 0;
    for (size_t i = 0; i < e->lexema_len;) {
        int L;
        unsigned cp = cp_decode(e->lexema, i, e->lexema_len, &L);
        if (cps == 0) primeiro = cp;
        cps++;
        i += (size_t)L;
    }
    if (cps == 1 && !mc_alnum_cp(primeiro)) return mk("diagnóstico de símbolo “%S” não reconhecido.", (Str){ e->lexema, e->lexema_len });
    return mk("diagnóstico léxico: %s “%S”.", e->erro, (Str){ e->lexema, e->lexema_len });
}

static Str mc_montar_rejeicao(const char *categoria, int codigo_saida, Str criterio) {
    return mk("Resultado: REJEITAR\nCategoria esperada: erro %s (código de saída %d)\nCritério: %S\n",
              categoria, codigo_saida, criterio);
}

static void mc_debug(const char *fmt, ...) {
    const char *d = getenv("MINIC_DEBUG");
    if (!d || !*d) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

//Saidas de sucesso
static Str mc_montar_saida_o2(const char *ir_otimizada) {
    return mk("Resultado: COMPILAR COM SUCESSO\nModo de referência: IR otimizada (-O2)\n"
              "IR otimizada (todas as otimizações):\n%s", ir_otimizada);
}

static Str mc_montar_saida_o1(const OtRelatorio *r) {
    StrBuf sb = {0};
    sb_add(&sb, "Resultado: COMPILAR COM SUCESSO");
    if (r->aviso) sb_add(&sb, " (aviso opcional de código inalcançável)");
    sb_add(&sb, "\nAlvo: "); sb_add(&sb, r->alvo);
    sb_add(&sb, "\nIR de referência (-O0):\n"); sb_add(&sb, r->ref);
    sb_addc(&sb, '\n'); sb_add(&sb, r->titulo);
    sb_addc(&sb, '\n'); sb_add(&sb, r->opt);
    for (int i = 0; i < r->nextras; i++) { sb_addc(&sb, '\n'); sb_add(&sb, r->extras[i]); }
    sb_addc(&sb, '\n');
    return str_from_sb(&sb);
}

//Cabecalho conforme os recursos que o gerador registrou
static Str mc_montar_saida(const GeradorIR *g, Str ir) {
    StrBuf sb = {0};
    sb_add(&sb, "Resultado: COMPILAR COM SUCESSO");
    if (g->usou_sombra) sb_add(&sb, " (assumindo a política MINIC de sombreamento léxico)");
    sb_add(&sb, "\nModo de referência: IR sem otimização (-O0)\nIR canônica esperada");
    if (g->usou_vetor) sb_add(&sb, " (LOAD/STORE equivalentes aceitos)");
    else if (g->usou_logico) sb_add(&sb, " (curto-circuito ou operação lógica explícita)");
    else if (g->usou_rotulos) sb_add(&sb, " (rótulos podem variar)");
    sb_add(&sb, ":\n");
    sb_add_n(&sb, ir.p, ir.n);
    if (g->usou_vetor) sb_add(&sb, MC_OBS_VETOR);
    if (g->usou_sombra) sb_add(&sb, MC_OBS_SOMBRA);
    return str_from_sb(&sb);
}

//Compila e devolve o codigo de saida; texto fica em *saida (n == 0 e p == NULL se nao ha)
static int mc_compilar(const char *codigo, size_t tamanho, int nivel, Str *saida) {
    saida->p = NULL;
    saida->n = 0;
    VetorTokens tokens = {0};
    VetorErros erros_lex = {0};
    scanner_n(codigo, (int)tamanho, &tokens, &erros_lex);
    normalizar_tokens(&tokens);
    Parser parser;
    memset(&parser, 0, sizeof parser);
    parser.tokens = tokens.dados;
    parser.ntokens = tokens.tamanho;
    Node *ast_c = parse_programa(&parser);

    //Lexico
    if (erros_lex.tamanho > 0) {
        for (int i = 0; i < erros_lex.tamanho; i++) {
            Erro *e = &erros_lex.dados[i];
            StrBuf sb = {0};
            repr_str(&sb, e->lexema, e->lexema_len);
            mc_debug("ERRO LÉXICO %d:%d: %s %s", e->linha, e->coluna, e->erro, sb.dados ? sb.dados : "");
            free(sb.dados);
        }
        *saida = mc_montar_rejeicao("léxico", MC_EXIT_LEXICO, mc_criterio_lexico(&erros_lex.dados[0]));
        return MC_EXIT_LEXICO;
    }
    //Sintatico
    if (parser.erros.tamanho > 0) {
        for (int i = 0; i < parser.erros.tamanho; i++) {
            ErroSint *e = &parser.erros.dados[i];
            mc_debug("ERRO SINTÁTICO %d:%d: %s", e->linha, e->coluna, e->mensagem);
        }
        *saida = mc_montar_rejeicao("sintático", MC_EXIT_SINTATICO, mc_criterio_sintatico(&parser.erros.dados[0], codigo));
        return MC_EXIT_SINTATICO;
    }
    //Semantico
    PV *ast = conv(ast_c);
    analisar(ast);
    if (ndiags > 0) {
        for (int i = 0; i < ndiags; i++)
            mc_debug("ERRO SEMÂNTICO %s — linha %d, coluna %d: %.*s", diags[i].codigo, diags[i].linha,
                     diags[i].coluna, (int)diags[i].msg.n, diags[i].msg.p);
        *saida = mc_montar_rejeicao("semântico", MC_EXIT_SEMANTICO, mc_criterio_semantico(diags[0].msg, ast));
        return MC_EXIT_SEMANTICO;
    }
    //Geracao de IR
    GeradorIR g;
    memset(&g, 0, sizeof g);
    Str ir = gi_gerar(&g, ast);
    if (nivel == 1) {
        OtRelatorio r = ot_otimizar_alvo(ir.p);
        *saida = mc_montar_saida_o1(&r);
        ot_relatorio_liberar(&r);
    } else if (nivel >= 2) {
        char *o = ot_otimizar(ir.p);
        *saida = mc_montar_saida_o2(o);
        free(o);
    } else {
        *saida = mc_montar_saida(&g, ir);
    }
    return MC_EXIT_OK;
}

//Nome do arquivo sem diretorio nem extensao
static char *mc_nome_base(const char *caminho) {
    const char *b = caminho;
    for (const char *p = caminho; *p; p++) {
        if (*p == '/') b = p + 1;
#ifdef _WIN32
        if (*p == '\\' || (*p == ':' && p == caminho + 1)) b = p + 1;
#endif
    }
    size_t n = strlen(b), fim = n;
    const char *ponto = strrchr(b, '.');
    if (ponto) {
        size_t idx = (size_t)(ponto - b);
        for (size_t k = 0; k < idx; k++) if (b[k] != '.') { fim = idx; break; }
    }
    return xmemdup(b, fim);
}

//Nome no formato O<digitos>
static int mc_nome_otimizacao(const char *nome) {
    if (nome[0] != 'O' || !nome[1]) return 0;
    for (const char *p = nome + 1; *p; p++) if (*p < '0' || *p > '9') return 0;
    return 1;
}

int minic_main(int argc, char *argv[]) __asm__(MINIC_ASM("main"));
int minic_main(int argc, char *argv[]) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    int nivel = 0;
    const char *env = getenv("MINIC_OPT");
    if (env) {
        if (strcmp(env, "1") == 0 || strcmp(env, "O1") == 0) nivel = 1;
        else if (strcmp(env, "2") == 0 || strcmp(env, "O2") == 0) nivel = 2;
    }
    const char *caminho = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-O0") == 0 || strcmp(argv[i], "-O1") == 0 || strcmp(argv[i], "-O2") == 0) nivel = argv[i][2] - '0';
        else if (!caminho) caminho = argv[i];
    }
    if (!caminho) {
        fputs("Uso: minic <arquivo>\n", stderr);
        return MC_EXIT_IO;
    }
    //Escolhe o nivel pelo nome do arquivo se nada foi pedido
    if (nivel == 0 && !getenv("MINIC_OPT") && mc_nome_otimizacao(mc_nome_base(caminho))) nivel = 1;
    size_t tamanho = 0;
    char *codigo = ler_arquivo(caminho, &tamanho);
    if (!codigo) {
        if (!ler_arquivo_utf8_invalido) fprintf(stderr, "Erro ao abrir arquivo: %s\n", caminho);
        return MC_EXIT_IO;
    }
    Str saida;
    int status = mc_compilar(codigo, tamanho, nivel, &saida);
    if (saida.p) fwrite(saida.p, 1, saida.n, stdout);
    return status;
}
