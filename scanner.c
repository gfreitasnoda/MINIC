#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

/* ------------------------------------------------------------------------
   Utilitarios compartilhados com parser.c (que inclui este arquivo)
   ------------------------------------------------------------------------ */
static void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) { fputs("Erro: memoria insuficiente\n", stderr); exit(1); }
    return p;
}

static void *xrealloc(void *ptr, size_t n) {
    void *p = realloc(ptr, n ? n : 1);
    if (!p) { fputs("Erro: memoria insuficiente\n", stderr); exit(1); }
    return p;
}

static char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *r = xmalloc(n);
    memcpy(r, s, n);
    return r;
}

/* Copia n bytes e acrescenta '\0' (n pode conter NULs internos). */
static char *xmemdup(const char *s, size_t n) {
    char *r = xmalloc(n + 1);
    if (n) memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

typedef struct {
    char  *dados;
    size_t tamanho;
    size_t capacidade;
} StrBuf;

static void sb_add_n(StrBuf *sb, const char *s, size_t n) {
    if (sb->tamanho + n + 1 > sb->capacidade) {
        size_t nova = sb->capacidade ? sb->capacidade : 256;
        while (sb->tamanho + n + 1 > nova) nova *= 2;
        sb->dados = xrealloc(sb->dados, nova);
        sb->capacidade = nova;
    }
    if (n) memcpy(sb->dados + sb->tamanho, s, n);
    sb->tamanho += n;
    sb->dados[sb->tamanho] = '\0';
}

static void sb_add(StrBuf *sb, const char *s) { sb_add_n(sb, s, strlen(s)); }
static void sb_addc(StrBuf *sb, char c)       { sb_add_n(sb, &c, 1); }

/* Reproduz repr/str(float) do Python: a MENOR quantidade de digitos que faz round-trip e,
   entre as de mesmo tamanho, a mais proxima do valor (modo 0 de David Gay).
   %.{p-1}e da o candidato corretamente arredondado; porem, nas fronteiras de potencia de 2
   o intervalo de round-trip e assimetrico e o candidato arredondado pode cair fora dele
   enquanto o vizinho (ultimo digito +-1) cai dentro. Por isso tambem se testam m+1 e m-1. */
static char *python_repr_float(double v) {
    if (isnan(v)) return xstrdup("nan");
    if (isinf(v)) return xstrdup(v < 0 ? "-inf" : "inf");
    if (v == 0.0) return xstrdup(signbit(v) ? "-0.0" : "0.0");

    int negativo = v < 0;
    if (negativo) v = -v;

    char sci[48];
    unsigned long long m = 0;
    int e10 = 0, achou = 0;
    for (int prec = 1; prec <= 17 && !achou; prec++) {
        snprintf(sci, sizeof(sci), "%.*e", prec - 1, v);
        const char *e = strchr(sci, 'e');
        unsigned long long mant = 0;
        for (const char *c = sci; c < e; c++) {
            if (isdigit((unsigned char)*c)) mant = mant * 10ULL + (unsigned long long)(*c - '0');
        }
        int exp10 = atoi(e + 1) - (prec - 1);          /* valor = mant * 10^exp10 */
        if (strtod(sci, NULL) == v) { m = mant; e10 = exp10; achou = 1; break; }
        for (int d = -1; d <= 1; d += 2) {
            if (d < 0 && mant == 0) continue;
            unsigned long long cand = d > 0 ? mant + 1ULL : mant - 1ULL;
            char t[64];
            snprintf(t, sizeof(t), "%lluE%d", cand, exp10);
            if (strtod(t, NULL) == v) { m = cand; e10 = exp10; achou = 1; break; }
        }
    }
    if (!achou) {                                       /* inatingivel: 17 digitos sempre bastam */
        snprintf(sci, sizeof(sci), "%.16e", v);
        const char *e = strchr(sci, 'e');
        m = 0;
        for (const char *c = sci; c < e; c++) if (isdigit((unsigned char)*c)) m = m * 10ULL + (unsigned long long)(*c - '0');
        e10 = atoi(e + 1) - 16;
    }

    char digitos[32];
    int nd = snprintf(digitos, sizeof(digitos), "%llu", m);
    while (nd > 1 && digitos[nd - 1] == '0') { digitos[--nd] = '\0'; e10++; }
    int expo = e10 + nd - 1;
    int decpt = expo + 1;

    char *saida = xmalloc(64);
    char *o = saida;
    if (negativo) *o++ = '-';

    if (decpt > 16 || decpt <= -4) {
        *o++ = digitos[0];
        if (nd > 1) {
            *o++ = '.';
            memcpy(o, digitos + 1, (size_t)(nd - 1));
            o += nd - 1;
        }
        o += sprintf(o, "e%c%02d", expo < 0 ? '-' : '+', abs(expo));
    } else if (decpt <= 0) {
        *o++ = '0';
        *o++ = '.';
        for (int i = 0; i < -decpt; i++) *o++ = '0';
        memcpy(o, digitos, (size_t)nd);
        o += nd;
    } else if (decpt >= nd) {
        memcpy(o, digitos, (size_t)nd);
        o += nd;
        for (int i = nd; i < decpt; i++) *o++ = '0';
        *o++ = '.';
        *o++ = '0';
    } else {
        memcpy(o, digitos, (size_t)decpt);
        o += decpt;
        *o++ = '.';
        memcpy(o, digitos + decpt, (size_t)(nd - decpt));
        o += nd - decpt;
    }
    *o = '\0';
    return saida;
}

/* Validacao estrita de UTF-8, equivalente ao decoder do Python (open(..., encoding="utf-8")):
   rejeita bytes de continuacao soltos, sequencias truncadas, overlongs, surrogates
   (U+D800..U+DFFF) e code points acima de U+10FFFF. */
static int utf8_valido(const unsigned char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        unsigned char c = s[i];
        size_t extra;
        unsigned char lo = 0x80, hi = 0xBF;
        if (c < 0x80)                { i++; continue; }
        else if (c >= 0xC2 && c <= 0xDF) { extra = 1; }
        else if (c == 0xE0)          { extra = 2; lo = 0xA0; }
        else if (c == 0xED)          { extra = 2; hi = 0x9F; }
        else if (c >= 0xE1 && c <= 0xEF) { extra = 2; }
        else if (c == 0xF0)          { extra = 3; lo = 0x90; }
        else if (c >= 0xF1 && c <= 0xF3) { extra = 3; }
        else if (c == 0xF4)          { extra = 3; hi = 0x8F; }
        else return 0;
        if (i + extra >= n) return 0;   /* sequencia truncada */
        if (s[i + 1] < lo || s[i + 1] > hi) return 0;
        for (size_t k = 2; k <= extra; k++) {
            if (s[i + k] < 0x80 || s[i + k] > 0xBF) return 0;
        }
        i += extra + 1;
    }
    return 1;
}

/* Indica a ler_arquivo()/main() que a falha foi UTF-8 invalido (e nao arquivo ausente). */
static int ler_arquivo_utf8_invalido = 0;

/* Le o arquivo inteiro e aplica a "universal newline" do open() em modo texto
   do Python: \r\n e \r isolado viram \n. Pode conter bytes NUL. */
static char *ler_arquivo(const char *caminho, size_t *tamanho_out) {
    FILE *f = fopen(caminho, "rb");
    if (!f) return NULL;

    size_t cap = 4096, n = 0;
    char *buf = xmalloc(cap);
    size_t lido;
    while ((lido = fread(buf + n, 1, cap - n - 1, f)) > 0) {
        n += lido;
        if (n + 1 >= cap) {
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
    }
    int erro_leitura = ferror(f);
    fclose(f);
    if (erro_leitura) { free(buf); return NULL; }   /* ex.: o caminho e um diretorio (Python: IsADirectoryError) */

    /* O Python decodifica o arquivo inteiro antes de qualquer saida: se for UTF-8
       invalido, aborta (UnicodeDecodeError) sem imprimir nada e com exit code 1. */
    if (!utf8_valido((const unsigned char *)buf, n)) {
        fputs("Erro: entrada com UTF-8 invalido\n", stderr);
        ler_arquivo_utf8_invalido = 1;
        free(buf);
        return NULL;
    }

    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        if (buf[i] == '\r') {
            buf[j++] = '\n';
            if (i + 1 < n && buf[i + 1] == '\n') i++;
        } else {
            buf[j++] = buf[i];
        }
    }
    buf[j] = '\0';
    *tamanho_out = j;
    return buf;
}

/* ------------------------------------------------------------------------
   Tabela de tokens (espelha TOKENS de scanner.py, na mesma ordem)
   ------------------------------------------------------------------------ */
typedef enum {
    MK_NUM_REAL,
    MK_NUM_INT,
    MK_ID,
    MK_LITERAL,
    MK_STRING_LIT,
    MK_CHAR_LIT,
    MK_ESPACO,
    MK_COMENTARIO_BLOCO,
    MK_COMENTARIO
} MatchKind;

typedef struct {
    const char *nome;
    MatchKind   kind;
    const char *literal;
    int         prioridade;
} TokenSpec;

static const TokenSpec TOKENS[] = {
    {"num_real",          MK_NUM_REAL,   NULL, 0},
    {"num_int",           MK_NUM_INT,    NULL, 0},
    {"id",                MK_ID,         NULL, 1},

    {"igualdade",         MK_LITERAL,    "==", 0},
    {"diferente",         MK_LITERAL,    "!=", 0},
    {"menor_igual",       MK_LITERAL,    "<=", 0},
    {"maior_igual",       MK_LITERAL,    ">=", 0},
    {"e_logico",          MK_LITERAL,    "&&", 0},
    {"ou_logico",         MK_LITERAL,    "||", 0},
    {"nao_logico",        MK_LITERAL,    "!",  0},

    {"atribuicao",        MK_LITERAL,    "=",  1},
    {"menor",             MK_LITERAL,    "<",  1},
    {"maior",             MK_LITERAL,    ">",  1},

    {"incremento",        MK_LITERAL,    "++", 0},
    {"decremento",        MK_LITERAL,    "--", 0},
    {"soma",              MK_LITERAL,    "+",  0},
    {"subtracao",         MK_LITERAL,    "-",  0},
    {"multiplicacao",     MK_LITERAL,    "*",  0},
    {"divisao",           MK_LITERAL,    "/",  0},
    {"modulo",            MK_LITERAL,    "%",  0},

    {"abre_parenteses",   MK_LITERAL,    "(",  0},
    {"fecha_parenteses",  MK_LITERAL,    ")",  0},
    {"abre_chave",        MK_LITERAL,    "{",  0},
    {"fecha_chave",       MK_LITERAL,    "}",  0},
    {"abre_colchete",     MK_LITERAL,    "[",  0},
    {"fecha_colchete",    MK_LITERAL,    "]",  0},
    {"ponto_virgula",     MK_LITERAL,    ";",  0},
    {"virgula",           MK_LITERAL,    ",",  0},
    {"ponto",             MK_LITERAL,    ".",  0},

    {"string_lit",        MK_STRING_LIT, NULL, 0},
    {"char_lit",          MK_CHAR_LIT,   NULL, 0},

    {"espaco",            MK_ESPACO,           NULL, 0},
    {"comentario_bloco",  MK_COMENTARIO_BLOCO, NULL, 0},
    {"comentario",        MK_COMENTARIO,       NULL, 0},
};
static const int NUM_TOKENS = sizeof(TOKENS) / sizeof(TOKENS[0]);

static const char *PALAVRAS_RESERVADAS[] = {
    "int", "float", "bool", "char", "void", "if", "else", "while", "for",
    "return", "break", "continue", "true", "false", "print", "read",
};
static const int NUM_PALAVRAS_RESERVADAS =
    sizeof(PALAVRAS_RESERVADAS) / sizeof(PALAVRAS_RESERVADAS[0]);

static int e_palavra_reservada(const char *lexema) {
    for (int i = 0; i < NUM_PALAVRAS_RESERVADAS; i++) {
        if (strcmp(lexema, PALAVRAS_RESERVADAS[i]) == 0) return 1;
    }
    return 0;
}

static const char *token_constante(const char *nome) {
    if (strcmp(nome, "num_real") == 0)          return "FLOAT_LIT";
    if (strcmp(nome, "num_int") == 0)           return "INT_LIT";
    if (strcmp(nome, "id") == 0)                return "IDENT";
    if (strcmp(nome, "igualdade") == 0)         return "EQ";
    if (strcmp(nome, "diferente") == 0)         return "NEQ";
    if (strcmp(nome, "menor_igual") == 0)       return "LE";
    if (strcmp(nome, "maior_igual") == 0)       return "GE";
    if (strcmp(nome, "e_logico") == 0)          return "AND";
    if (strcmp(nome, "ou_logico") == 0)         return "OR";
    if (strcmp(nome, "nao_logico") == 0)        return "NOT";
    if (strcmp(nome, "atribuicao") == 0)        return "ASSIGN";
    if (strcmp(nome, "menor") == 0)             return "LT";
    if (strcmp(nome, "maior") == 0)             return "GT";
    if (strcmp(nome, "incremento") == 0)        return "INCREMENT";
    if (strcmp(nome, "decremento") == 0)        return "DECREMENT";
    if (strcmp(nome, "soma") == 0)              return "PLUS";
    if (strcmp(nome, "subtracao") == 0)         return "MINUS";
    if (strcmp(nome, "multiplicacao") == 0)     return "STAR";
    if (strcmp(nome, "divisao") == 0)           return "SLASH";
    if (strcmp(nome, "modulo") == 0)            return "PERCENT";
    if (strcmp(nome, "abre_parenteses") == 0)   return "LPAREN";
    if (strcmp(nome, "fecha_parenteses") == 0)  return "RPAREN";
    if (strcmp(nome, "abre_chave") == 0)        return "LBRACE";
    if (strcmp(nome, "fecha_chave") == 0)       return "RBRACE";
    if (strcmp(nome, "abre_colchete") == 0)     return "LBRACKET";
    if (strcmp(nome, "fecha_colchete") == 0)    return "RBRACKET";
    if (strcmp(nome, "ponto_virgula") == 0)     return "SEMICOLON";
    if (strcmp(nome, "virgula") == 0)           return "COMMA";
    if (strcmp(nome, "ponto") == 0)             return "DOT";
    if (strcmp(nome, "string_lit") == 0)        return "STRING_LIT";
    if (strcmp(nome, "char_lit") == 0)          return "CHAR_LIT";
    return NULL;
}

static char *para_maiusculas(const char *s) {
    char *r = xstrdup(s);
    for (char *p = r; *p; p++) *p = (char)toupper((unsigned char)*p);
    return r;
}

/* ------------------------------------------------------------------------
   Tokens e erros
   ------------------------------------------------------------------------ */
/* ATTR_INT: attr_string guarda os digitos decimais ja normalizados (sem zeros
   a esquerda), porque o Python usa int() de precisao arbitraria. */
typedef enum { ATTR_NULL, ATTR_INT, ATTR_FLOAT, ATTR_STRING } AttrType;

typedef struct {
    char    *token;
    char    *lexema;
    size_t   lexema_len;
    AttrType attr_type;
    double   attr_float;
    char    *attr_string;
    size_t   attr_len;
    int      linha;
    int      coluna;
} Token;

typedef struct {
    char  *erro;
    char  *lexema;
    size_t lexema_len;
    int    linha;
    int    coluna;
} Erro;

typedef struct {
    Token *dados;
    int    tamanho;
    int    capacidade;
} VetorTokens;

typedef struct {
    Erro *dados;
    int   tamanho;
    int   capacidade;
} VetorErros;

static void vetor_tokens_add(VetorTokens *v, Token t) {
    if (v->tamanho == v->capacidade) {
        v->capacidade = v->capacidade ? v->capacidade * 2 : 16;
        v->dados = xrealloc(v->dados, (size_t)v->capacidade * sizeof(Token));
    }
    v->dados[v->tamanho++] = t;
}

static void vetor_erros_add(VetorErros *v, Erro e) {
    if (v->tamanho == v->capacidade) {
        v->capacidade = v->capacidade ? v->capacidade * 2 : 16;
        v->dados = xrealloc(v->dados, (size_t)v->capacidade * sizeof(Erro));
    }
    v->dados[v->tamanho++] = e;
}

static Token criar_token(const char *token, const char *lexema, size_t lexema_len,
                         AttrType attr_type, double attr_float,
                         const char *attr_string, size_t attr_len,
                         int linha, int coluna) {
    Token t;
    t.token = xstrdup(token);
    t.lexema = xmemdup(lexema, lexema_len);
    t.lexema_len = lexema_len;
    t.attr_type = attr_type;
    t.attr_float = attr_float;
    t.attr_string = attr_string ? xmemdup(attr_string, attr_len) : NULL;
    t.attr_len = attr_string ? attr_len : 0;
    t.linha = linha;
    t.coluna = coluna;
    return t;
}

static Erro criar_erro(const char *codigo_erro, const char *lexema, size_t lexema_len,
                       int linha, int coluna) {
    Erro e;
    e.erro = xstrdup(codigo_erro);
    e.lexema = xmemdup(lexema, lexema_len);
    e.lexema_len = lexema_len;
    e.linha = linha;
    e.coluna = coluna;
    return e;
}

/* ------------------------------------------------------------------------
   Saida textual (espelha formatar_valor / token_para_texto / erro_para_texto)
   formatar_valor() so escapa \ " \n \r \t; qualquer outro byte sai cru.
   ------------------------------------------------------------------------ */
static void sb_add_valor_str(StrBuf *sb, const char *s, size_t n) {
    sb_addc(sb, '"');
    for (size_t i = 0; i < n; i++) {
        switch (s[i]) {
            case '\\': sb_add(sb, "\\\\"); break;
            case '"':  sb_add(sb, "\\\""); break;
            case '\n': sb_add(sb, "\\n");  break;
            case '\r': sb_add(sb, "\\r");  break;
            case '\t': sb_add(sb, "\\t");  break;
            default:   sb_addc(sb, s[i]);
        }
    }
    sb_addc(sb, '"');
}

static void sb_add_int(StrBuf *sb, int v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", v);
    sb_add(sb, buf);
}

static void token_para_texto(StrBuf *sb, const Token *t) {
    sb_add(sb, "{\"token\":");
    sb_add_valor_str(sb, t->token, strlen(t->token));
    sb_add(sb, ",\"lexeme\":");
    sb_add_valor_str(sb, t->lexema, t->lexema_len);
    sb_add(sb, ",\"attribute\":");
    switch (t->attr_type) {
        case ATTR_NULL:   sb_add(sb, "null"); break;
        case ATTR_INT:    sb_add_n(sb, t->attr_string, t->attr_len); break;
        case ATTR_FLOAT: {
            char *txt = python_repr_float(t->attr_float);
            sb_add(sb, txt);
            free(txt);
            break;
        }
        case ATTR_STRING: sb_add_valor_str(sb, t->attr_string, t->attr_len); break;
    }
    sb_add(sb, ",\"line\":");
    sb_add_int(sb, t->linha);
    sb_add(sb, ",\"column\":");
    sb_add_int(sb, t->coluna);
    sb_addc(sb, '}');
}

static void erro_para_texto(StrBuf *sb, const Erro *e) {
    sb_add(sb, "{\"erro\":");
    sb_add_valor_str(sb, e->erro, strlen(e->erro));
    sb_add(sb, ",\"lexema\":");
    sb_add_valor_str(sb, e->lexema, e->lexema_len);
    sb_add(sb, ",\"linha\":");
    sb_add_int(sb, e->linha);
    sb_add(sb, ",\"coluna\":");
    sb_add_int(sb, e->coluna);
    sb_addc(sb, '}');
}

/* ------------------------------------------------------------------------
   UTF-8: o Python trabalha com code points (colunas, '.' das regex, len()).
   Aqui trabalhamos com bytes, mas contamos colunas e consumimos "um caractere"
   em code points.
   ------------------------------------------------------------------------ */
static int cp_len(const char *s, int pos, int tamanho) {
    unsigned char c = (unsigned char)s[pos];
    int n = 1;
    if (c >= 0xF0 && c <= 0xF7)      n = 4;
    else if (c >= 0xE0)              n = (c <= 0xEF) ? 3 : 1;
    else if (c >= 0xC0)              n = (c <= 0xDF) ? 2 : 1;
    if (pos + n > tamanho) n = tamanho - pos;
    return n;
}

/* Quantidade de code points em [de, ate) = bytes que nao sao de continuacao. */
static int contar_cps(const char *s, int de, int ate) {
    int n = 0;
    for (int i = de; i < ate; i++) {
        if (((unsigned char)s[i] & 0xC0) != 0x80) n++;
    }
    return n;
}

/* Avanca linha/coluna sobre [de, ate), como o laco "for caractere in lexema". */
static void avancar_posicao(const char *s, int de, int ate, int *linha, int *coluna) {
    for (int i = de; i < ate; i++) {
        if (s[i] == '\n') {
            *linha += 1;
            *coluna = 1;
        } else if (((unsigned char)s[i] & 0xC0) != 0x80) {
            *coluna += 1;
        }
    }
}

/* ------------------------------------------------------------------------
   Casamento dos padroes
   ------------------------------------------------------------------------ */
static int casar_spec(const TokenSpec *spec, const char *codigo, int pos, int tamanho) {
    switch (spec->kind) {
        case MK_NUM_REAL: {            /* [0-9]+\.[0-9]+ */
            int i = pos;
            while (i < tamanho && isdigit((unsigned char)codigo[i])) i++;
            if (i == pos) return 0;
            if (i >= tamanho || codigo[i] != '.') return 0;
            int j = i + 1;
            int inicio_frac = j;
            while (j < tamanho && isdigit((unsigned char)codigo[j])) j++;
            if (j == inicio_frac) return 0;
            return j - pos;
        }
        case MK_NUM_INT: {             /* [0-9]+ */
            int i = pos;
            while (i < tamanho && isdigit((unsigned char)codigo[i])) i++;
            return i - pos;
        }
        case MK_ID: {                  /* [a-zA-Z_][a-zA-Z0-9_]* */
            int i = pos;
            if (i >= tamanho) return 0;
            char c = codigo[i];
            if (!(isalpha((unsigned char)c) || c == '_')) return 0;
            i++;
            while (i < tamanho && (isalnum((unsigned char)codigo[i]) || codigo[i] == '_')) i++;
            return i - pos;
        }
        case MK_LITERAL: {
            int len = (int)strlen(spec->literal);
            if (pos + len > tamanho) return 0;
            if (strncmp(codigo + pos, spec->literal, (size_t)len) == 0) return len;
            return 0;
        }
        case MK_STRING_LIT: {          /* "(?:\\.|[^"\\\n])*"   ('.' nao casa \n) */
            if (pos >= tamanho || codigo[pos] != '"') return 0;
            int i = pos + 1;
            for (;;) {
                if (i >= tamanho) return 0;
                char c = codigo[i];
                if (c == '"') return i - pos + 1;
                if (c == '\n') return 0;
                if (c == '\\') {
                    if (i + 1 >= tamanho || codigo[i + 1] == '\n') return 0;
                    i += 2;
                    continue;
                }
                i++;
            }
        }
        case MK_CHAR_LIT: {            /* '(?:\\.|[^'\\\n])'  -> exatamente 1 code point */
            if (pos >= tamanho || codigo[pos] != '\'') return 0;
            int i = pos + 1;
            if (i >= tamanho) return 0;
            char c = codigo[i];
            if (c == '\\') {
                if (i + 1 >= tamanho || codigo[i + 1] == '\n') return 0;
                i += 1 + cp_len(codigo, i + 1, tamanho);
            } else if (c == '\'' || c == '\n') {
                return 0;
            } else {
                i += cp_len(codigo, i, tamanho);
            }
            if (i >= tamanho || codigo[i] != '\'') return 0;
            return i - pos + 1;
        }
        case MK_ESPACO: {              /* [ \t\r\n]+ */
            int i = pos;
            while (i < tamanho && (codigo[i] == ' ' || codigo[i] == '\t' ||
                                    codigo[i] == '\r' || codigo[i] == '\n')) i++;
            return i - pos;
        }
        case MK_COMENTARIO_BLOCO: {    /* regex: bloco de comentario nao guloso, ate o primeiro fecha-comentario */
            if (pos + 1 >= tamanho) return 0;
            if (codigo[pos] != '/' || codigo[pos + 1] != '*') return 0;
            int i = pos + 2;
            while (i + 1 < tamanho && !(codigo[i] == '*' && codigo[i + 1] == '/')) i++;
            if (i + 1 >= tamanho) return 0;
            return (i + 2) - pos;
        }
        case MK_COMENTARIO: {          /* regex: // ate o fim da linha */
            if (pos + 1 >= tamanho) return 0;
            if (codigo[pos] != '/' || codigo[pos + 1] != '/') return 0;
            int i = pos + 2;
            while (i < tamanho && codigo[i] != '\n') i++;
            return i - pos;
        }
    }
    return 0;
}

static const TokenSpec *casar_maximal_munch(const char *codigo, int pos, int tamanho, int *comprimento_out) {
    const TokenSpec *melhor = NULL;
    int melhor_comprimento = 0;
    int melhor_prioridade = 0;

    for (int i = 0; i < NUM_TOKENS; i++) {
        const TokenSpec *spec = &TOKENS[i];
        int comprimento = casar_spec(spec, codigo, pos, tamanho);
        if (comprimento == 0) continue;

        int maior = comprimento > melhor_comprimento;
        int empate_com_prioridade = (comprimento == melhor_comprimento) &&
                                     (melhor != NULL) &&
                                     (spec->prioridade < melhor_prioridade);

        if (melhor == NULL || maior || empate_com_prioridade) {
            melhor = spec;
            melhor_comprimento = comprimento;
            melhor_prioridade = spec->prioridade;
        }
    }

    *comprimento_out = melhor_comprimento;
    return melhor;
}

/* encontrar_fim_recuperacao: para antes de \n, ')', ']' ou '}' (ou no fim). */
static int encontrar_fim_recuperacao(const char *codigo, int pos, int tamanho) {
    while (pos < tamanho) {
        char c = codigo[pos];
        if (c == '\n' || c == ')' || c == ']' || c == '}') return pos;
        pos++;
    }
    return tamanho;
}

static int tem_fecha_comentario(const char *codigo, int de, int tamanho) {
    for (int i = de; i + 1 < tamanho; i++) {
        if (codigo[i] == '*' && codigo[i + 1] == '/') return 1;
    }
    return 0;
}

/* processar_escapes: \n \t \r \0 \\ \" \' ; qualquer outro gera
   INVALID_ESCAPE_SEQUENCE e mantem o caractere apos a barra. */
static void processar_escapes(const char *bruto, size_t n, int linha, int coluna_inicial,
                              VetorErros *erros, StrBuf *saida) {
    size_t b = 0;
    int i = 0;                         /* indice em code points, como no Python */
    while (b < n) {
        if (bruto[b] == '\\' && b + 1 < n) {
            int L = cp_len(bruto, (int)b + 1, (int)n);
            char prox = bruto[b + 1];
            int ok = 0;
            char mapeado = 0;
            if (L == 1) {
                switch (prox) {
                    case 'n':  mapeado = '\n'; ok = 1; break;
                    case 't':  mapeado = '\t'; ok = 1; break;
                    case 'r':  mapeado = '\r'; ok = 1; break;
                    case '0':  mapeado = '\0'; ok = 1; break;
                    case '\\': mapeado = '\\'; ok = 1; break;
                    case '"':  mapeado = '"';  ok = 1; break;
                    case '\'': mapeado = '\''; ok = 1; break;
                    default: break;
                }
            }
            if (ok) {
                sb_addc(saida, mapeado);
            } else {
                char *lex = xmalloc((size_t)L + 2);
                lex[0] = '\\';
                memcpy(lex + 1, bruto + b + 1, (size_t)L);
                vetor_erros_add(erros, criar_erro("INVALID_ESCAPE_SEQUENCE", lex, (size_t)L + 1,
                                                  linha, coluna_inicial + i));
                free(lex);
                sb_add_n(saida, bruto + b + 1, (size_t)L);
            }
            b += 1 + (size_t)L;
            i += 2;
        } else {
            int L = cp_len(bruto, (int)b, (int)n);
            sb_add_n(saida, bruto + b, (size_t)L);
            b += (size_t)L;
            i += 1;
        }
    }
}

/* ------------------------------------------------------------------------
   Scanner principal (espelha scanner() de scanner.py)
   ------------------------------------------------------------------------ */
static void scanner_n(const char *codigo, int tamanho, VetorTokens *tokens, VetorErros *erros) {
    int inicio_lexema = 0;
    int linha = 1;
    int coluna = 1;

    while (inicio_lexema < tamanho) {
        /* comentario de bloco nao terminado */
        if (inicio_lexema + 1 < tamanho &&
            codigo[inicio_lexema] == '/' && codigo[inicio_lexema + 1] == '*' &&
            !tem_fecha_comentario(codigo, inicio_lexema + 2, tamanho)) {
            int fim = encontrar_fim_recuperacao(codigo, inicio_lexema + 2, tamanho);
            vetor_erros_add(erros, criar_erro("UNTERMINATED_BLOCK_COMMENT",
                                              codigo + inicio_lexema, (size_t)(fim - inicio_lexema),
                                              linha, coluna));
            coluna += contar_cps(codigo, inicio_lexema, fim);
            inicio_lexema = fim;
            continue;
        }

        int comprimento = 0;
        const TokenSpec *spec = casar_maximal_munch(codigo, inicio_lexema, tamanho, &comprimento);

        if (spec == NULL) {
            /* --- literal de char invalido --- */
            if (codigo[inicio_lexema] == '\'') {
                int fim_linha = tamanho;
                for (int i = inicio_lexema; i < tamanho; i++) {
                    if (codigo[i] == '\n') { fim_linha = i; break; }
                }

                int pos_fechamento = -1;
                int pos = inicio_lexema + 1;
                while (pos < fim_linha) {
                    if (codigo[pos] == '\\' && pos + 1 < fim_linha) {
                        pos += 2;
                        continue;
                    }
                    if (codigo[pos] == '\'') {
                        pos_fechamento = pos;
                        break;
                    }
                    pos++;
                }

                if (pos_fechamento < 0) {
                    int fim = encontrar_fim_recuperacao(codigo, inicio_lexema + 1, tamanho);
                    vetor_erros_add(erros, criar_erro("UNTERMINATED_CHAR_LITERAL",
                                                      codigo + inicio_lexema, (size_t)(fim - inicio_lexema),
                                                      linha, coluna));
                    coluna += contar_cps(codigo, inicio_lexema, fim);
                    inicio_lexema = fim;
                } else {
                    int len_erro = pos_fechamento + 1 - inicio_lexema;
                    const char *codigo_erro_char =
                        (pos_fechamento == inicio_lexema + 1) ? "EMPTY_CHAR_LITERAL"
                                                              : "INVALID_CHAR_LITERAL";
                    vetor_erros_add(erros, criar_erro(codigo_erro_char,
                                                      codigo + inicio_lexema, (size_t)len_erro,
                                                      linha, coluna));
                    coluna += contar_cps(codigo, inicio_lexema, inicio_lexema + len_erro);
                    inicio_lexema += len_erro;
                }
                continue;
            }

            /* --- literal de string invalido (nao terminada) --- */
            if (codigo[inicio_lexema] == '"') {
                int fim = encontrar_fim_recuperacao(codigo, inicio_lexema + 1, tamanho);
                vetor_erros_add(erros, criar_erro("UNTERMINATED_STRING_LITERAL",
                                                  codigo + inicio_lexema, (size_t)(fim - inicio_lexema),
                                                  linha, coluna));
                coluna += contar_cps(codigo, inicio_lexema, fim);
                inicio_lexema = fim;
                continue;
            }

            /* --- simbolo desconhecido: UM code point --- */
            int L = cp_len(codigo, inicio_lexema, tamanho);
            vetor_erros_add(erros, criar_erro("UNKNOWN_SYMBOL", codigo + inicio_lexema, (size_t)L,
                                              linha, coluna));
            if (codigo[inicio_lexema] == '\n') {
                linha += 1;
                coluna = 1;
            } else {
                coluna += 1;
            }
            inicio_lexema += L;
            continue;
        }

        const char *lexema = codigo + inicio_lexema;
        int avanco = inicio_lexema + comprimento;
        int linha_token = linha;
        int coluna_token = coluna;

        avancar_posicao(codigo, inicio_lexema, avanco, &linha, &coluna);

        if (strcmp(spec->nome, "espaco") == 0 ||
            strcmp(spec->nome, "comentario") == 0 ||
            strcmp(spec->nome, "comentario_bloco") == 0) {
            /* ignorado */
        } else if (strcmp(spec->nome, "id") == 0) {
            char *lex = xmemdup(lexema, (size_t)comprimento);
            if (e_palavra_reservada(lex)) {
                char *tok = para_maiusculas(lex);
                vetor_tokens_add(tokens, criar_token(tok, lexema, (size_t)comprimento,
                                                     ATTR_NULL, 0, NULL, 0,
                                                     linha_token, coluna_token));
                free(tok);
            } else {
                vetor_tokens_add(tokens, criar_token("IDENT", lexema, (size_t)comprimento,
                                                     ATTR_STRING, 0, lexema, (size_t)comprimento,
                                                     linha_token, coluna_token));
            }
            free(lex);
        } else if (strcmp(spec->nome, "num_int") == 0) {
            const char *d = lexema;
            int n = comprimento;
            while (n > 1 && d[0] == '0') { d++; n--; }
            vetor_tokens_add(tokens, criar_token("INT_LIT", lexema, (size_t)comprimento,
                                                 ATTR_INT, 0, d, (size_t)n,
                                                 linha_token, coluna_token));
        } else if (strcmp(spec->nome, "num_real") == 0) {
            char *lex = xmemdup(lexema, (size_t)comprimento);
            double valor = strtod(lex, NULL);
            free(lex);
            vetor_tokens_add(tokens, criar_token("FLOAT_LIT", lexema, (size_t)comprimento,
                                                 ATTR_FLOAT, valor, NULL, 0,
                                                 linha_token, coluna_token));
        } else if (strcmp(spec->nome, "string_lit") == 0 || strcmp(spec->nome, "char_lit") == 0) {
            StrBuf proc = {0};
            processar_escapes(lexema + 1, (size_t)comprimento - 2, linha_token, coluna_token + 1,
                              erros, &proc);
            const char *tok = token_constante(spec->nome);
            vetor_tokens_add(tokens, criar_token(tok, lexema, (size_t)comprimento,
                                                 ATTR_STRING, 0,
                                                 proc.dados ? proc.dados : "", proc.tamanho,
                                                 linha_token, coluna_token));
            free(proc.dados);
        } else {
            const char *tok = token_constante(spec->nome);
            vetor_tokens_add(tokens, criar_token(tok, lexema, (size_t)comprimento,
                                                 ATTR_NULL, 0, NULL, 0,
                                                 linha_token, coluna_token));
        }

        inicio_lexema = avanco;
    }

    vetor_tokens_add(tokens, criar_token("EOF", "", 0, ATTR_NULL, 0, NULL, 0, linha, coluna));
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);   /* "\n" literal, sem traducao para "\r\n" */
#endif
    if (argc < 2) {
        fputs("Uso: scanner <arquivo>\n", stderr);
        return 1;
    }

    size_t tamanho = 0;
    char *codigo = ler_arquivo(argv[1], &tamanho);
    if (!codigo) {
        if (!ler_arquivo_utf8_invalido) fprintf(stderr, "Erro ao abrir arquivo: %s\n", argv[1]);
        return 1;
    }

    VetorTokens tokens = {0};
    VetorErros erros = {0};
    scanner_n(codigo, (int)tamanho, &tokens, &erros);

    StrBuf sb = {0};
    for (int i = 0; i < tokens.tamanho; i++) {
        token_para_texto(&sb, &tokens.dados[i]);
        sb_addc(&sb, '\n');
    }
    for (int i = 0; i < erros.tamanho; i++) {
        erro_para_texto(&sb, &erros.dados[i]);
        sb_addc(&sb, '\n');
    }
    if (sb.dados) fwrite(sb.dados, 1, sb.tamanho, stdout);

    free(sb.dados);
    for (int i = 0; i < tokens.tamanho; i++) {
        free(tokens.dados[i].token);
        free(tokens.dados[i].lexema);
        free(tokens.dados[i].attr_string);
    }
    free(tokens.dados);
    for (int i = 0; i < erros.tamanho; i++) {
        free(erros.dados[i].erro);
        free(erros.dados[i].lexema);
    }
    free(erros.dados);
    free(codigo);

    return 0;
}
