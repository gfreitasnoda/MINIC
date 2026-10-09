#define main scanner_main
#include "scanner.c"
#undef main

#include <stdarg.h>
#include <setjmp.h>
#include <math.h>

typedef enum {
    N_PROGRAM, N_FUNCTION, N_PARAM, N_VARDECL, N_BLOCK, N_IF, N_WHILE, N_FOR,
    N_RETURN, N_BREAK, N_CONTINUE, N_PRINT, N_READ, N_EXPRSTMT,
    N_ASSIGN, N_BINARY, N_UNARY, N_CALL, N_INDEX, N_ID, N_LIT
} NodeKind;

typedef struct Node Node;

typedef struct {
    Node **dados;
    int    tamanho;
    int    capacidade;
} NodeList;

struct Node {
    NodeKind kind;
    int      linha;
    int      coluna;
    int      vetor;   /* Param: 1 se declarado como tipo IDENT[] */
    char    *tipo;
    char    *nome;
    char    *op;
    char    *texto;
    size_t   texto_len;
    Node    *f1, *f2, *f3, *f4;
    NodeList lista;
    Node    *proximo_alocado;
};

static void lista_add(NodeList *l, Node *n) {
    if (l->tamanho == l->capacidade) {
        l->capacidade = l->capacidade ? l->capacidade * 2 : 8;
        l->dados = xrealloc(l->dados, (size_t)l->capacidade * sizeof(Node *));
    }
    l->dados[l->tamanho++] = n;
}

typedef struct {
    char  *lexema;
    size_t lexema_len;
    int    linha;
    int   coluna;
    char *mensagem;
    size_t mensagem_len;
} ErroSint;

typedef struct {
    ErroSint *dados;
    int       tamanho;
    int       capacidade;
} VetorErrosSint;

/* Contrato comum com parser.py: limites de aninhamento (mesmos valores e mesma mensagem). */
#define MAX_PROF_PARSE 1000
#define MAX_PROF_AST   10000
static const char *const MSG_LIMITE = "Erro: limite de aninhamento excedido\n";

typedef struct {
    Token          *tokens;
    int             ntokens;
    int             pos;
    int             prof;
    VetorErrosSint  erros;
    jmp_buf        *handler;
    Node           *nos;
} Parser;

/* Tokens em que é seguro retomar a análise após um erro */
static const char *const TIPOS[] = { "INT", "FLOAT", "BOOL", "CHAR", "VOID", NULL };

static const char *const SYNC_DECL[] = {
    "INT", "FLOAT", "BOOL", "CHAR", "VOID", "EOF", NULL
};

static const char *const SYNC_STATEMENT[] = {
    "INT", "FLOAT", "BOOL", "CHAR", "VOID",
    "SEMICOLON", "RBRACE", "EOF",
    "IF", "ELSE", "WHILE", "FOR", "RETURN", "BREAK", "CONTINUE",
    "PRINT", "READ", "LBRACE", NULL
};

static const char *const OPS_OU[]      = { "OR", NULL };
static const char *const OPS_E[]       = { "AND", NULL };
static const char *const OPS_IGUAL[]   = { "EQ", "NEQ", NULL };
static const char *const OPS_REL[]     = { "LT", "LE", "GT", "GE", NULL };
static const char *const OPS_ADIT[]    = { "PLUS", "MINUS", NULL };
static const char *const OPS_MULT[]    = { "STAR", "SLASH", "PERCENT", NULL };

/* Construção de nós da AST */
static Node *novo_no(Parser *p, NodeKind kind, int linha, int coluna) {
    Node *n = xmalloc(sizeof(Node));
    memset(n, 0, sizeof(Node));
    n->kind = kind;
    n->linha = linha;
    n->coluna = coluna;
    n->proximo_alocado = p->nos;
    p->nos = n;
    return n;
}

/* Infraestrutura */
static Token *atual(Parser *p) {
    return &p->tokens[p->pos];
}

static int token_em(const Token *t, const char *const *tipos) {
    for (int i = 0; tipos[i] != NULL; i++) {
        if (strcmp(t->token, tipos[i]) == 0) return 1;
    }
    return 0;
}

static int verifica_lista(Parser *p, const char *const *tipos) {
    return token_em(atual(p), tipos);
}

static int verifica_va(Parser *p, ...) {
    va_list ap;
    va_start(ap, p);
    int achou = 0;
    const char *t;
    while ((t = va_arg(ap, const char *)) != NULL) {
        if (strcmp(atual(p)->token, t) == 0) { achou = 1; break; }
    }
    va_end(ap);
    return achou;
}
#define verifica(p, ...) verifica_va((p), __VA_ARGS__, NULL)

static Token *avancar(Parser *p) {
    Token *tok = &p->tokens[p->pos];
    if (strcmp(tok->token, "EOF") != 0) p->pos++;
    return tok;
}

static Token *casar(Parser *p, const char *tipo) {
    if (verifica(p, tipo)) return avancar(p);
    return NULL;
}

static void registrar_erro(Parser *p, const char *contexto, const char *esperado) {
    Token *tok = atual(p);
    const char *achado = tok->lexema[0] ? tok->lexema : tok->token;
    const char *pre = esperado ? "; esperado " : "";
    const char *esp = esperado ? esperado : "";

    /* length-aware: o lexema de um STRING_LIT pode conter NUL (Python mantem o lexema inteiro). */
    size_t achado_len = tok->lexema[0] ? tok->lexema_len : strlen(tok->token);
    StrBuf mb = {0};
    sb_add(&mb, "token inesperado '");
    sb_add_n(&mb, achado, achado_len);
    sb_addc(&mb, '\'');
    sb_add(&mb, pre);
    sb_add(&mb, esp);
    sb_add(&mb, "; contexto: ");
    sb_add(&mb, contexto);
    char *msg = mb.dados;

    VetorErrosSint *v = &p->erros;
    if (v->tamanho == v->capacidade) {
        v->capacidade = v->capacidade ? v->capacidade * 2 : 8;
        v->dados = xrealloc(v->dados, (size_t)v->capacidade * sizeof(ErroSint));
    }
    ErroSint e;
    e.lexema = xmemdup(tok->lexema, tok->lexema_len);
    e.lexema_len = tok->lexema_len;
    e.linha = tok->linha;
    e.coluna = tok->coluna;
    e.mensagem = msg;
    e.mensagem_len = mb.tamanho;
    v->dados[v->tamanho++] = e;
}

/* Usado internamente para abortar a producao atual e acionar recuperacao. */
static void lancar(Parser *p) {
    if (p->handler) longjmp(*p->handler, 1);
    exit(1);
}

static int tentar(Parser *p, Node *(*fn)(Parser *), Node **out) {
    jmp_buf buf;
    jmp_buf *anterior = p->handler;
    int prof_salva = p->prof;
    p->handler = &buf;
    if (setjmp(buf) == 0) {
        *out = fn(p);
        p->handler = anterior;
        return 1;
    }
    p->handler = anterior;
    p->prof = prof_salva;
    return 0;
}

static Token *esperar(Parser *p, const char *tipo, const char *contexto) {
    if (verifica(p, tipo)) return avancar(p);
    registrar_erro(p, contexto, tipo);
    lancar(p);
    return NULL;
}

static void sincronizar(Parser *p, const char *const *tokens_sincronizacao) {
    while (!verifica(p, "EOF")) {
        if (verifica_lista(p, tokens_sincronizacao)) return;
        avancar(p);
    }
}

static Node *parse_declaracao_global(Parser *p);
static Node *parse_funcao(Parser *p, const char *tipo, const char *nome, int linha, int coluna);
static Node *parse_param(Parser *p);
static Node *parse_var_resto(Parser *p, const char *tipo, const char *nome, int linha, int coluna);
static Node *parse_bloco(Parser *p);
static Node *parse_item_bloco(Parser *p);
static Node *parse_declaracao_local(Parser *p);
static Node *parse_comando(Parser *p);
static Node *parse_if(Parser *p);
static Node *parse_while(Parser *p);
static Node *parse_for(Parser *p);
static Node *parse_return(Parser *p);
static Node *parse_break(Parser *p);
static Node *parse_continue(Parser *p);
static Node *parse_print(Parser *p);
static Node *parse_read(Parser *p);
static Node *parse_comando_expr(Parser *p);
static Node *parse_expressao(Parser *p);
static Node *parse_atribuicao(Parser *p);
static Node *parse_ou_logico(Parser *p);
static Node *parse_e_logico(Parser *p);
static Node *parse_igualdade(Parser *p);
static Node *parse_relacional(Parser *p);
static Node *parse_aditiva(Parser *p);
static Node *parse_multiplicativa(Parser *p);
static Node *parse_unario(Parser *p);
static Node *parse_posfixo(Parser *p);
static Node *parse_primario(Parser *p);
static Node *parse_comando_impl(Parser *p);
static Node *parse_atribuicao_impl(Parser *p);
static Node *parse_unario_impl(Parser *p);

static void entrar_prof(Parser *p) {
    if (++p->prof > MAX_PROF_PARSE) {
        fputs(MSG_LIMITE, stderr);
        exit(1);
    }
}
static Node *parse_comando(Parser *p)   { entrar_prof(p); Node *r = parse_comando_impl(p);   p->prof--; return r; }
static Node *parse_atribuicao(Parser *p){ entrar_prof(p); Node *r = parse_atribuicao_impl(p); p->prof--; return r; }
static Node *parse_unario(Parser *p)    { entrar_prof(p); Node *r = parse_unario_impl(p);    p->prof--; return r; }

/* Programa e Declarações globais */
static Node *parse_programa(Parser *p) {
    Node *programa = novo_no(p, N_PROGRAM, 0, 0);
    while (!verifica(p, "EOF")) {
        Node *decl = NULL;
        if (tentar(p, parse_declaracao_global, &decl)) {
            lista_add(&programa->lista, decl);
        } else {
            sincronizar(p, SYNC_DECL);
        }
    }
    return programa;
}

static const char *parse_tipo(Parser *p) {
    Token *tok = atual(p);
    if (verifica_lista(p, TIPOS)) {
        avancar(p);
        return tok->lexema;
    }
    registrar_erro(p, "tipo", "tipo (int/float/bool/char/void)");
    lancar(p);
    return NULL;
}

static Node *parse_declaracao_global(Parser *p) {
    if (!verifica_lista(p, TIPOS)) return parse_comando(p);

    Token *tok0 = atual(p);
    int linha = tok0->linha, coluna = tok0->coluna;
    const char *tipo = parse_tipo(p);
    Token *nome_tok = esperar(p, "IDENT", "nome apos tipo");
    const char *nome = nome_tok->lexema;

    if (verifica(p, "LPAREN")) return parse_funcao(p, tipo, nome, linha, coluna);
    return parse_var_resto(p, tipo, nome, linha, coluna);
}

static Node *parse_funcao(Parser *p, const char *tipo, const char *nome, int linha, int coluna) {
    esperar(p, "LPAREN", "abertura de parametros");
    Node *fn = novo_no(p, N_FUNCTION, linha, coluna);
    fn->tipo = xstrdup(tipo);
    fn->nome = xstrdup(nome);

    if (!verifica(p, "RPAREN")) {
        lista_add(&fn->lista, parse_param(p));
        while (casar(p, "COMMA")) {
            lista_add(&fn->lista, parse_param(p));
        }
    }
    esperar(p, "RPAREN", "fechamento de parametros");
    fn->f1 = parse_bloco(p);
    return fn;
}

static Node *parse_param(Parser *p) {
    Token *tok = atual(p);
    const char *tipo = parse_tipo(p);
    Token *nome_tok = esperar(p, "IDENT", "nome do parametro");
    int vetor = 0;
    if (casar(p, "LBRACKET")) {
        esperar(p, "RBRACKET", "fechamento de parametro vetor");
        vetor = 1;
    }
    Node *n = novo_no(p, N_PARAM, tok->linha, tok->coluna);
    n->tipo = xstrdup(tipo);
    n->nome = xstrdup(nome_tok->lexema);
    n->vetor = vetor;
    return n;
}

static Node *parse_var_resto(Parser *p, const char *tipo, const char *nome, int linha, int coluna) {
    Node *tamanho = NULL;
    Node *valor = NULL;

    if (casar(p, "LBRACKET")) {
        tamanho = parse_expressao(p);
        esperar(p, "RBRACKET", "fechamento de vetor");
    } else if (casar(p, "ASSIGN")) {
        valor = parse_expressao(p);
    }

    esperar(p, "SEMICOLON", "termino de declaracao");
    Node *n = novo_no(p, N_VARDECL, linha, coluna);
    n->tipo = xstrdup(tipo);
    n->nome = xstrdup(nome);
    n->f1 = tamanho;
    n->f2 = valor;
    return n;
}

/* Blocos e Declarações locais */
static Node *parse_bloco(Parser *p) {
    Token *abre = esperar(p, "LBRACE", "abertura de bloco");
    Node *bloco = novo_no(p, N_BLOCK, abre->linha, abre->coluna);

    while (!verifica(p, "RBRACE", "EOF")) {
        int pos_antes = p->pos;
        Node *item = NULL;
        if (tentar(p, parse_item_bloco, &item)) {
            lista_add(&bloco->lista, item);
        } else {
            sincronizar(p, SYNC_STATEMENT);
            if (verifica(p, "SEMICOLON")) {
                avancar(p);
            } else if (p->pos == pos_antes) {
                avancar(p);
            }
        }
    }
    esperar(p, "RBRACE", "fechamento de bloco");
    return bloco;
}

static Node *parse_item_bloco(Parser *p) {
    if (verifica_lista(p, TIPOS)) return parse_declaracao_local(p);
    return parse_comando(p);
}

static Node *parse_declaracao_local(Parser *p) {
    Token *tok0 = atual(p);
    const char *tipo = parse_tipo(p);
    Token *nome_tok = esperar(p, "IDENT", "nome apos tipo");
    return parse_var_resto(p, tipo, nome_tok->lexema, tok0->linha, tok0->coluna);
}

/* Comandos */
static Node *parse_comando_impl(Parser *p) {
    if (verifica(p, "LBRACE"))   return parse_bloco(p);
    if (verifica(p, "IF"))       return parse_if(p);
    if (verifica(p, "WHILE"))    return parse_while(p);
    if (verifica(p, "FOR"))      return parse_for(p);
    if (verifica(p, "RETURN"))   return parse_return(p);
    if (verifica(p, "BREAK"))    return parse_break(p);
    if (verifica(p, "CONTINUE")) return parse_continue(p);
    if (verifica(p, "PRINT"))    return parse_print(p);
    if (verifica(p, "READ"))     return parse_read(p);
    return parse_comando_expr(p);
}

static Node *parse_if(Parser *p) {
    Token *tok = avancar(p);  /* 'if' */
    esperar(p, "LPAREN", "condicao do if");
    Node *condicao = parse_expressao(p);
    esperar(p, "RPAREN", "fechamento da condicao do if");
    Node *entao = parse_comando(p);
    Node *senao = NULL;
    if (casar(p, "ELSE")) {
        senao = parse_comando(p);
    }
    Node *n = novo_no(p, N_IF, tok->linha, tok->coluna);
    n->f1 = condicao;
    n->f2 = entao;
    n->f3 = senao;
    return n;
}

static Node *parse_while(Parser *p) {
    Token *tok = avancar(p);
    esperar(p, "LPAREN", "condicao do while");
    Node *condicao = parse_expressao(p);
    esperar(p, "RPAREN", "fechamento da condicao do while");
    Node *corpo = parse_comando(p);
    Node *n = novo_no(p, N_WHILE, tok->linha, tok->coluna);
    n->f1 = condicao;
    n->f2 = corpo;
    return n;
}

static Node *parse_for(Parser *p) {
    Token *tok = avancar(p);  /* 'for' */
    esperar(p, "LPAREN", "abertura do for");

    Node *init = NULL;
    if (verifica_lista(p, TIPOS)) {
        Token *tok_i = atual(p);
        const char *tipo = parse_tipo(p);
        Token *nome_tok = esperar(p, "IDENT", "nome no init do for");
        Node *valor = NULL;
        if (casar(p, "ASSIGN")) {
            valor = parse_expressao(p);
        }
        init = novo_no(p, N_VARDECL, tok_i->linha, tok_i->coluna);
        init->tipo = xstrdup(tipo);
        init->nome = xstrdup(nome_tok->lexema);
        init->f1 = NULL;
        init->f2 = valor;
    } else if (!verifica(p, "SEMICOLON")) {
        Token *tok_e = atual(p);
        Node *expr = parse_expressao(p);
        init = novo_no(p, N_EXPRSTMT, tok_e->linha, tok_e->coluna);
        init->f1 = expr;
    }
    esperar(p, "SEMICOLON", "separador do for (apos inicializacao)");

    Node *condicao = NULL;
    if (!verifica(p, "SEMICOLON")) {
        condicao = parse_expressao(p);
    }
    esperar(p, "SEMICOLON", "separador do for (apos condicao)");

    Node *incremento = NULL;
    if (!verifica(p, "RPAREN")) {
        incremento = parse_expressao(p);
    }
    esperar(p, "RPAREN", "fechamento do for");

    Node *corpo = parse_comando(p);
    Node *n = novo_no(p, N_FOR, tok->linha, tok->coluna);
    n->f1 = init;
    n->f2 = condicao;
    n->f3 = incremento;
    n->f4 = corpo;
    return n;
}

static Node *parse_return(Parser *p) {
    Token *tok = avancar(p);
    Node *valor = NULL;
    if (!verifica(p, "SEMICOLON")) {
        valor = parse_expressao(p);
    }
    esperar(p, "SEMICOLON", "termino do return");
    Node *n = novo_no(p, N_RETURN, tok->linha, tok->coluna);
    n->f1 = valor;
    return n;
}

static Node *parse_break(Parser *p) {
    Token *tok = avancar(p);
    esperar(p, "SEMICOLON", "termino do break");
    return novo_no(p, N_BREAK, tok->linha, tok->coluna);
}

static Node *parse_continue(Parser *p) {
    Token *tok = avancar(p);
    esperar(p, "SEMICOLON", "termino do continue");
    return novo_no(p, N_CONTINUE, tok->linha, tok->coluna);
}

static Node *parse_print(Parser *p) {
    Token *tok = avancar(p);
    esperar(p, "LPAREN", "abertura do print");
    Node *valor = parse_expressao(p);
    esperar(p, "RPAREN", "fechamento do print");
    esperar(p, "SEMICOLON", "termino do print");
    Node *n = novo_no(p, N_PRINT, tok->linha, tok->coluna);
    n->f1 = valor;
    return n;
}

static Node *parse_read(Parser *p) {
    Token *tok = avancar(p);
    esperar(p, "LPAREN", "abertura do read");
    Node *alvo = parse_posfixo(p);
    if (alvo->kind != N_ID && alvo->kind != N_INDEX) {

        registrar_erro(p, "alvo do read precisa ser uma variavel ou posicao de vetor", NULL);
    }
    esperar(p, "RPAREN", "fechamento do read");
    esperar(p, "SEMICOLON", "termino do read");
    Node *n = novo_no(p, N_READ, tok->linha, tok->coluna);
    n->f1 = alvo;
    return n;
}

static Node *parse_comando_expr(Parser *p) {
    Token *tok = atual(p);
    if (casar(p, "SEMICOLON")) {
        return novo_no(p, N_EXPRSTMT, tok->linha, tok->coluna);
    }
    Node *expr = parse_expressao(p);
    esperar(p, "SEMICOLON", "termino da expressao");
    Node *n = novo_no(p, N_EXPRSTMT, tok->linha, tok->coluna);
    n->f1 = expr;
    return n;
}

/* Expressões */
static Node *parse_expressao(Parser *p) {
    return parse_atribuicao(p);
}

static Node *parse_atribuicao_impl(Parser *p) {
    Node *esquerda = parse_ou_logico(p);
    if (verifica(p, "ASSIGN")) {
        avancar(p);
        Node *direita = parse_atribuicao(p);
        Node *n = novo_no(p, N_ASSIGN, esquerda->linha, esquerda->coluna);
        n->f1 = esquerda;
        n->f2 = direita;
        return n;
    }
    return esquerda;
}

static Node *binario_esquerda(Parser *p, Node *(*proximo_nivel)(Parser *), const char *const *operadores) {
    Node *resultado = proximo_nivel(p);
    while (verifica_lista(p, operadores)) {
        Token *tok = avancar(p);
        Node *direita = proximo_nivel(p);
        Node *n = novo_no(p, N_BINARY, tok->linha, tok->coluna);
        n->op = xstrdup(tok->lexema);
        n->f1 = resultado;
        n->f2 = direita;
        resultado = n;
    }
    return resultado;
}

static Node *parse_ou_logico(Parser *p)      { return binario_esquerda(p, parse_e_logico, OPS_OU); }
static Node *parse_e_logico(Parser *p)       { return binario_esquerda(p, parse_igualdade, OPS_E); }
static Node *parse_igualdade(Parser *p)      { return binario_esquerda(p, parse_relacional, OPS_IGUAL); }
static Node *parse_relacional(Parser *p)     { return binario_esquerda(p, parse_aditiva, OPS_REL); }
static Node *parse_aditiva(Parser *p)        { return binario_esquerda(p, parse_multiplicativa, OPS_ADIT); }
static Node *parse_multiplicativa(Parser *p) { return binario_esquerda(p, parse_unario, OPS_MULT); }

static Node *parse_unario_impl(Parser *p) {
    if (verifica(p, "MINUS", "NOT")) {
        Token *tok = avancar(p);
        Node *operando = parse_unario(p);
        Node *n = novo_no(p, N_UNARY, tok->linha, tok->coluna);
        n->op = xstrdup(tok->lexema);
        n->f1 = operando;
        return n;
    }
    return parse_posfixo(p);
}

static Node *parse_posfixo(Parser *p) {
    Node *expr = parse_primario(p);
    for (;;) {
        if (verifica(p, "LPAREN")) {
            Token *tok = avancar(p);
            Node *chamada = novo_no(p, N_CALL, tok->linha, tok->coluna);
            if (!verifica(p, "RPAREN")) {
                lista_add(&chamada->lista, parse_expressao(p));
                while (casar(p, "COMMA")) {
                    lista_add(&chamada->lista, parse_expressao(p));
                }
            }
            esperar(p, "RPAREN", "fechamento de chamada");
            chamada->f1 = expr;
            expr = chamada;
        } else if (verifica(p, "LBRACKET")) {
            Token *tok = avancar(p);
            Node *indice = parse_expressao(p);
            esperar(p, "RBRACKET", "fechamento de indice");
            Node *n = novo_no(p, N_INDEX, tok->linha, tok->coluna);
            n->f1 = expr;
            n->f2 = indice;
            expr = n;
        } else {
            break;
        }
    }
    return expr;
}

static Node *novo_literal(Parser *p, const Token *tok, const char *tipo,
                         const char *texto, size_t len) {
    Node *n = novo_no(p, N_LIT, tok->linha, tok->coluna);
    n->tipo = xstrdup(tipo);
    n->texto = xmemdup(texto, len);
    n->texto_len = len;
    return n;
}

static Node *parse_primario(Parser *p) {
    Token *tok = atual(p);

    if (casar(p, "IDENT")) {
        Node *n = novo_no(p, N_ID, tok->linha, tok->coluna);
        n->nome = xstrdup(tok->lexema);
        return n;
    }
    if (casar(p, "INT_LIT"))    return novo_literal(p, tok, "int",    tok->lexema, strlen(tok->lexema));
    if (casar(p, "FLOAT_LIT"))  return novo_literal(p, tok, "float",  tok->lexema, strlen(tok->lexema));
    if (casar(p, "STRING_LIT")) return novo_literal(p, tok, "string", tok->attr_string, tok->attr_len);
    if (casar(p, "CHAR_LIT"))   return novo_literal(p, tok, "char",   tok->attr_string, tok->attr_len);
    if (casar(p, "TRUE"))       return novo_literal(p, tok, "bool",   "true", 4);
    if (casar(p, "FALSE"))      return novo_literal(p, tok, "bool",   "false", 5);
    if (casar(p, "LPAREN")) {
        Node *expr = parse_expressao(p);
        esperar(p, "RPAREN", "fechamento de agrupamento");
        return expr;
    }

    registrar_erro(p, "expressao", "expressão");
    lancar(p);
    return NULL;
}

static void sb_add_json_string(StrBuf *sb, const char *s, size_t n) {
    sb_addc(sb, '"');
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"':  sb_add(sb, "\\\""); break;
            case '\\': sb_add(sb, "\\\\"); break;
            case '\n': sb_add(sb, "\\n");  break;
            case '\r': sb_add(sb, "\\r");  break;
            case '\t': sb_add(sb, "\\t");  break;
            case '\b': sb_add(sb, "\\b");  break;
            case '\f': sb_add(sb, "\\f");  break;
            default:
                if (c < 0x20) {
                    char tmp[8];
                    snprintf(tmp, sizeof(tmp), "\\u%04x", c);
                    sb_add(sb, tmp);
                } else {
                    sb_addc(sb, (char)c);
                }
        }
    }
    sb_addc(sb, '"');
}

/* Renderização da AST em texto */
static int prof_render = 0;

static void renderizar_no(StrBuf *sb, const Node *n);

static void renderizar(StrBuf *sb, const Node *n) {
    if (n == NULL) { sb_add(sb, "NULL"); return; }
    if (++prof_render > MAX_PROF_AST) {
        fputs(MSG_LIMITE, stderr);
        exit(1);
    }
    renderizar_no(sb, n);
    prof_render--;
}

static void renderizar_no(StrBuf *sb, const Node *n) {

    switch (n->kind) {
    case N_PROGRAM:
        sb_add(sb, "Program(");
        for (int i = 0; i < n->lista.tamanho; i++) {
            if (i) sb_add(sb, ", ");
            renderizar(sb, n->lista.dados[i]);
        }
        sb_add(sb, ")");
        break;

    case N_FUNCTION:
        sb_add(sb, "Function(");
        sb_add(sb, n->tipo);
        sb_addc(sb, ' ');
        sb_add(sb, n->nome);
        sb_addc(sb, '(');
        for (int i = 0; i < n->lista.tamanho; i++) {
            if (i) sb_addc(sb, ',');
            sb_add(sb, n->lista.dados[i]->tipo);
            sb_addc(sb, ' ');
            sb_add(sb, n->lista.dados[i]->nome);
        }
        sb_add(sb, ") ");
        renderizar(sb, n->f1);
        sb_addc(sb, ')');
        break;

    case N_BLOCK:
        sb_add(sb, "Block(");
        for (int i = 0; i < n->lista.tamanho; i++) {
            if (i) sb_add(sb, ", ");
            renderizar(sb, n->lista.dados[i]);
        }
        sb_add(sb, ")");
        break;

    case N_VARDECL:
        sb_add(sb, "VarDecl(");
        sb_add(sb, n->tipo);
        sb_addc(sb, ' ');
        sb_add(sb, n->nome);
        if (n->f1 != NULL) {
            sb_add(sb, " size=");
            renderizar(sb, n->f1);
        } else if (n->f2 != NULL) {
            sb_addc(sb, '=');
            renderizar(sb, n->f2);
        }
        sb_addc(sb, ')');
        break;

    case N_IF:
        sb_add(sb, "If(");
        renderizar(sb, n->f1);
        sb_addc(sb, ',');
        renderizar(sb, n->f2);
        sb_addc(sb, ',');
        renderizar(sb, n->f3);
        sb_addc(sb, ')');
        break;

    case N_WHILE:
        sb_add(sb, "While(");
        renderizar(sb, n->f1);
        sb_addc(sb, ',');
        renderizar(sb, n->f2);
        sb_addc(sb, ')');
        break;

    case N_FOR:
        sb_add(sb, "For(");
        renderizar(sb, n->f1);
        sb_addc(sb, ',');
        renderizar(sb, n->f2);
        sb_addc(sb, ',');
        renderizar(sb, n->f3);
        sb_addc(sb, ',');
        renderizar(sb, n->f4);
        sb_addc(sb, ')');
        break;

    case N_RETURN:
        sb_add(sb, "Return(");
        renderizar(sb, n->f1);
        sb_addc(sb, ')');
        break;

    case N_BREAK:
        sb_add(sb, "Break()");
        break;

    case N_CONTINUE:
        sb_add(sb, "Continue()");
        break;

    case N_PRINT:
        sb_add(sb, "Print(");
        renderizar(sb, n->f1);
        sb_addc(sb, ')');
        break;

    case N_READ:
        sb_add(sb, "Read(");
        renderizar(sb, n->f1);
        sb_addc(sb, ')');
        break;

    case N_EXPRSTMT:
        sb_add(sb, "ExprStmt(");
        renderizar(sb, n->f1);
        sb_addc(sb, ')');
        break;

    case N_ASSIGN:
        sb_add(sb, "Assign(");
        renderizar(sb, n->f1);
        sb_addc(sb, ',');
        renderizar(sb, n->f2);
        sb_addc(sb, ')');
        break;

    case N_BINARY:
        sb_add(sb, "Binary(");
        sb_add(sb, n->op);
        sb_addc(sb, ',');
        renderizar(sb, n->f1);
        sb_addc(sb, ',');
        renderizar(sb, n->f2);
        sb_addc(sb, ')');
        break;

    case N_UNARY:
        sb_add(sb, "Unary(");
        sb_add(sb, n->op);
        sb_addc(sb, ',');
        renderizar(sb, n->f1);
        sb_addc(sb, ')');
        break;

    case N_CALL:
        sb_add(sb, "Call(");
        renderizar(sb, n->f1);
        for (int i = 0; i < n->lista.tamanho; i++) {
            sb_addc(sb, ',');
            renderizar(sb, n->lista.dados[i]);
        }
        sb_addc(sb, ')');
        break;

    case N_INDEX:
        sb_add(sb, "Index(");
        renderizar(sb, n->f1);
        sb_addc(sb, ',');
        renderizar(sb, n->f2);
        sb_addc(sb, ')');
        break;

    case N_ID:
        sb_add(sb, "Id(");
        sb_add(sb, n->nome);
        sb_addc(sb, ')');
        break;

    case N_LIT:
        sb_add(sb, "Lit(");
        if (strcmp(n->tipo, "float") == 0) {
            char *txt = python_repr_float(strtod(n->texto, NULL));
            sb_add(sb, "real,");
            sb_add(sb, txt);
            free(txt);
        } else if (strcmp(n->tipo, "string") == 0) {
            sb_add(sb, "string,");
            sb_add_json_string(sb, n->texto, n->texto_len);
        } else if (strcmp(n->tipo, "char") == 0) {
            sb_add(sb, "char,'");
            for (size_t k = 0; k < n->texto_len; k++) {
                if (n->texto[k] == '\'') sb_add(sb, "\\'");
                else sb_addc(sb, n->texto[k]);
            }
            sb_addc(sb, '\'');
        } else if (strcmp(n->tipo, "bool") == 0) {
            sb_add(sb, "bool,");
            sb_add(sb, n->texto);
        } else {
            const char *t = n->texto;
            while (t[0] == '0' && t[1] != '\0') t++;
            sb_add(sb, "int,");
            sb_add(sb, t);
        }
        sb_addc(sb, ')');
        break;

    default:
        sb_add(sb, "Param(?)");
        break;
    }
}

static const char *MSG_REJEITADO = "NÃO HÁ AST: o parser deve rejeitar a entrada.";

static char *formatar_resultado(const char *codigo, size_t tamanho, size_t *saida_len) {
    VetorTokens tokens = {0};
    VetorErros erros_lexicos = {0};
    scanner_n(codigo, (int)tamanho, &tokens, &erros_lexicos);

    Parser parser;
    memset(&parser, 0, sizeof(parser));
    parser.tokens = tokens.dados;
    parser.ntokens = tokens.tamanho;
    Node *ast = parse_programa(&parser);

    StrBuf sb = {0};
#ifdef DUMP_ERROS
    /* Somente para o teste diferencial: lista erros lexicos e sintaticos em stderr. */
    {
        StrBuf eb = {0};
        for (int i = 0; i < erros_lexicos.tamanho; i++) { erro_para_texto(&eb, &erros_lexicos.dados[i]); sb_addc(&eb, '\n'); }
        for (int i = 0; i < parser.erros.tamanho; i++) {
            sb_add(&eb, "{\"erro\":\"SYNTAX_ERROR\",\"lexema\":");
            sb_add_valor_str(&eb, parser.erros.dados[i].lexema, parser.erros.dados[i].lexema_len);
            sb_add(&eb, ",\"linha\":"); sb_add_int(&eb, parser.erros.dados[i].linha);
            sb_add(&eb, ",\"coluna\":"); sb_add_int(&eb, parser.erros.dados[i].coluna);
            sb_add(&eb, ",\"mensagem\":");
            sb_add_valor_str(&eb, parser.erros.dados[i].mensagem, parser.erros.dados[i].mensagem_len);
            sb_addc(&eb, '}'); sb_addc(&eb, '\n');
        }
        if (eb.dados) fwrite(eb.dados, 1, eb.tamanho, stderr);
        free(eb.dados);
    }
#endif
    if (erros_lexicos.tamanho > 0 || parser.erros.tamanho > 0) {
        sb_add(&sb, MSG_REJEITADO);
    } else {
        renderizar(&sb, ast);
    }

    for (Node *n = parser.nos; n != NULL; ) {
        Node *prox = n->proximo_alocado;
        free(n->tipo);
        free(n->nome);
        free(n->op);
        free(n->texto);
        free(n->lista.dados);
        free(n);
        n = prox;
    }
    for (int i = 0; i < parser.erros.tamanho; i++) {
        free(parser.erros.dados[i].lexema);
        free(parser.erros.dados[i].mensagem);
    }
    free(parser.erros.dados);

    for (int i = 0; i < tokens.tamanho; i++) {
        free(tokens.dados[i].token);
        free(tokens.dados[i].lexema);
        free(tokens.dados[i].attr_string);
    }
    free(tokens.dados);
    for (int i = 0; i < erros_lexicos.tamanho; i++) {
        free(erros_lexicos.dados[i].erro);
        free(erros_lexicos.dados[i].lexema);
    }
    free(erros_lexicos.dados);

    *saida_len = sb.tamanho;
    return sb.dados;
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);   /* "\n" literal, sem traducao para "\r\n" */
#endif
    if (argc < 2) {
        fputs("Uso: parser <arquivo>\n", stderr);
        return 1;
    }

    size_t tamanho = 0;
    char *codigo = ler_arquivo(argv[1], &tamanho);
    if (!codigo) {
        if (!ler_arquivo_utf8_invalido) fprintf(stderr, "Erro ao abrir arquivo: %s\n", argv[1]);
        return 1;
    }

    size_t saida_len = 0;
    char *saida = formatar_resultado(codigo, tamanho, &saida_len);

    fwrite(saida, 1, saida_len, stdout);
    fputc('\n', stdout);

    free(saida);
    free(codigo);
    return 0;
}
