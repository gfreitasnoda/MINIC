import os
import re
import sys

# Contrato de equivalencia com scanner.c: inteiros de precisao arbitraria (sem o limite
# de 4300 digitos do int() do Python 3.11+).
if hasattr(sys, "set_int_max_str_digits"):
    sys.set_int_max_str_digits(0)

TOKENS = [
    ("num_real",      r"[0-9]+\.[0-9]+", 0),
    ("num_int",       r"[0-9]+",         0),
    ("id",            r"[a-zA-Z_][a-zA-Z0-9_]*", 1),

    ("igualdade",     r"==", 0),
    ("diferente",     r"!=", 0),
    ("menor_igual",   r"<=", 0),
    ("maior_igual",   r">=", 0),
    ("e_logico",      r"&&", 0),
    ("ou_logico",     r"\|\|", 0),
    ("nao_logico",    r"!", 0),

    ("atribuicao",    r"=", 1),
    ("menor",         r"<", 1),
    ("maior",         r">", 1),

    ("incremento",    r"\+\+", 0),
    ("decremento",    r"--", 0),
    ("soma",          r"\+", 0),
    ("subtracao",     r"-", 0),
    ("multiplicacao", r"\*", 0),
    ("divisao",       r"/", 0),
    ("modulo",        r"%", 0),

    ("abre_parenteses",  r"\(", 0),
    ("fecha_parenteses", r"\)", 0),
    ("abre_chave",       r"\{", 0),
    ("fecha_chave",      r"\}", 0),
    ("abre_colchete",    r"\[", 0),
    ("fecha_colchete",   r"\]", 0),
    ("ponto_virgula",    r";", 0),
    ("virgula",          r",", 0),
    ("ponto",            r"\.", 0),

    # --- literais de string e char agora suportam sequencias de escape ---
    # \\.  -> qualquer caractere escapado (\", \', \\, \n, \t, etc.)
    # o literal so fecha em uma aspa/apostrofo que NAO esteja escapado
    ("string_lit",       r'"(?:\\.|[^"\\\n])*"', 0),
    # char_lit agora aceita exatamente UM caractere (normal ou escapado)
    # entre apostrofos, em vez de qualquer sequencia como antes
    ("char_lit",         r"'(?:\\.|[^'\\\n])'", 0),

    ("espaco",           r"[ \t\r\n]+", 0),
    ("comentario_bloco", r"(?s)/\*.*?\*/", 0),
    ("comentario",       r"//[^\n]*", 0),
]

TOKENS_COMPILADOS = [
    (nome, re.compile(regex), prioridade) for nome, regex, prioridade in TOKENS
]

PALAVRAS_RESERVADAS = {
    "int", "float", "bool", "char", "void", "if", "else", "while", "for",
    "return", "break", "continue", "true", "false", "print", "read",
}

TOKEN_CONSTANTE = {
    "num_real":          "FLOAT_LIT",
    "num_int":            "INT_LIT",
    "id":                 "IDENT",

    "igualdade":          "EQ",
    "diferente":          "NEQ",
    "menor_igual":        "LE",
    "maior_igual":        "GE",
    "e_logico":           "AND",
    "ou_logico":          "OR",
    "nao_logico":         "NOT",

    "atribuicao":         "ASSIGN",
    "menor":              "LT",
    "maior":              "GT",

    "incremento":         "INCREMENT",
    "decremento":         "DECREMENT",
    "soma":               "PLUS",
    "subtracao":          "MINUS",
    "multiplicacao":      "STAR",
    "divisao":            "SLASH",
    "modulo":             "PERCENT",

    "abre_parenteses":    "LPAREN",
    "fecha_parenteses":   "RPAREN",
    "abre_chave":         "LBRACE",
    "fecha_chave":        "RBRACE",
    "abre_colchete":      "LBRACKET",
    "fecha_colchete":     "RBRACKET",
    "ponto_virgula":      "SEMICOLON",
    "virgula":            "COMMA",
    "ponto":              "DOT",

    "string_lit":         "STRING_LIT",
    "char_lit":           "CHAR_LIT",
}

TOKEN_PALAVRA_RESERVADA = {
    palavra: palavra.upper() for palavra in PALAVRAS_RESERVADAS
}

# --- Tabela de sequencias de escape suportadas -----------------------------
ESCAPES_VALIDOS = {
    "n":  "\n",
    "t":  "\t",
    "r":  "\r",
    "0":  "\0",
    "\\": "\\",
    '"':  '"',
    "'":  "'",
}

def formatar_valor(valor):
    if valor is None:
        return "null"
    if isinstance(valor, str):
        escapado = (
            valor.replace("\\", "\\\\")
                 .replace('"', '\\"')
                 .replace("\n", "\\n")
                 .replace("\r", "\\r")
                 .replace("\t", "\\t")
        )
        return f'"{escapado}"'
    return str(valor)

def criar_token(token, lexema, atributo, linha, coluna):
    return {
        "token": token,
        "lexeme": lexema,
        "attribute": atributo,
        "line": linha,
        "column": coluna,
    }

def token_para_texto(dicionario_token):
    partes = [
        f'"token":{formatar_valor(dicionario_token["token"])}',
        f'"lexeme":{formatar_valor(dicionario_token["lexeme"])}',
        f'"attribute":{formatar_valor(dicionario_token["attribute"])}',
        f'"line":{formatar_valor(dicionario_token["line"])}',
        f'"column":{formatar_valor(dicionario_token["column"])}',
    ]
    return "{" + ",".join(partes) + "}"

def criar_erro(codigo_erro, lexema, linha, coluna):
    return {
        "erro": codigo_erro,
        "lexema": lexema,
        "linha": linha,
        "coluna": coluna,
    }

def erro_para_texto(dicionario_erro):
    partes = [
        f'"erro":{formatar_valor(dicionario_erro["erro"])}',
        f'"lexema":{formatar_valor(dicionario_erro["lexema"])}',
        f'"linha":{formatar_valor(dicionario_erro["linha"])}',
        f'"coluna":{formatar_valor(dicionario_erro["coluna"])}',
    ]
    return "{" + ",".join(partes) + "}"

def processar_escapes(conteudo_bruto, linha, coluna_inicial, erros):
    """
    Converte as sequencias de escape de um literal (string ou char) em seus
    caracteres reais (\\n -> quebra de linha, \\" -> aspas, etc.).
    Sequencias de escape desconhecidas (ex: \\q) geram um erro
    INVALID_ESCAPE_SEQUENCE mas nao interrompem a leitura: o caractere apos
    a barra e mantido literalmente para permitir a recuperacao.
    """
    resultado = []
    i = 0
    tamanho = len(conteudo_bruto)

    while i < tamanho:
        caractere = conteudo_bruto[i]

        if caractere == "\\" and i + 1 < tamanho:
            proximo = conteudo_bruto[i + 1]
            if proximo in ESCAPES_VALIDOS:
                resultado.append(ESCAPES_VALIDOS[proximo])
            else:
                erros.append(criar_erro(
                    codigo_erro="INVALID_ESCAPE_SEQUENCE",
                    lexema="\\" + proximo,
                    linha=linha,
                    coluna=coluna_inicial + i,
                ))
                resultado.append(proximo)
            i += 2
        else:
            resultado.append(caractere)
            i += 1

    return "".join(resultado)

def casar_maximal_munch(codigo, posicao):
    melhor_nome = None
    melhor_lexema = ""
    melhor_prioridade = None

    for nome, padrao, prioridade in TOKENS_COMPILADOS:
        resultado = padrao.match(codigo, posicao)
        if not resultado:
            continue
        lexema = resultado.group()

        maior = len(lexema) > len(melhor_lexema)
        empate_com_prioridade = (
            len(lexema) == len(melhor_lexema)
            and melhor_prioridade is not None
            and prioridade < melhor_prioridade
        )

        if melhor_nome is None or maior or empate_com_prioridade:
            melhor_nome = nome
            melhor_lexema = lexema
            melhor_prioridade = prioridade

    return melhor_nome, melhor_lexema

SYNC_CHARS_RECUPERACAO = set(")]}")

def encontrar_fim_recuperacao(codigo, pos_inicio_busca, tamanho):
    """
    Usado para delimitar o "lexema de erro" de uma string/char/comentario de
    bloco nao terminados. A partir de 'pos_inicio_busca' (logo apos o(s)
    caractere(s) de abertura ja contados no erro), avanca ate encontrar:
      - um fechamento de agrupamento (')' ']' '}') -> PARA imediatamente
        antes dele (nao o consome), pois ele provavelmente fecha a
        chamada/expressao onde a construcao mal formada apareceu (ex.: o
        ')' que fecha a chamada em volta de uma string sem aspas de
        fechamento). ';' e ',' NAO contam como sincronizacao: sao apenas
        finalizadores comuns de linha e nao indicam que o literal quebrado
        terminou ali.
      - uma quebra de linha ou o fim do arquivo -> para exatamente ali (a
        quebra de linha em si nao e consumida; sera tratada normalmente na
        proxima iteracao pelo token "espaco").
    Retorna a posicao de parada (exclusiva).
    """
    pos = pos_inicio_busca
    while pos < tamanho:
        c = codigo[pos]
        if c == "\n" or c in SYNC_CHARS_RECUPERACAO:
            return pos
        pos += 1
    return tamanho


def scanner(codigo):
    tokens = []
    erros = []

    inicio_lexema = 0
    avanco = 0
    linha = 1
    coluna = 1

    tamanho = len(codigo)

    while inicio_lexema < tamanho:
        # --- comentario de bloco nao terminado -----------------------------
        # Historico: primeiro consumia ate o EOF (perdia todo o resto do
        # arquivo); depois passou a consumir so os 2 caracteres "/*" (mas
        # isso fazia o conteudo do comentario quebrado virar tokens soltos
        # em vez de ficar coberto pelo erro).
        # Agora: o erro cobre do "/*" ate o primeiro caractere de
        # resincronizacao (")]}") ou ate o fim da linha, o que vier
        # primeiro - assim um token valido que exista logo depois do "/*"
        # na mesma linha (ex.: o ")" que fecha uma chamada) ainda e
        # reconhecido na posicao certa, sem transformar o texto do
        # comentario em identificadores/numeros soltos.
        if codigo.startswith("/*", inicio_lexema) and "*/" not in codigo[inicio_lexema + 2:]:
            fim = encontrar_fim_recuperacao(codigo, inicio_lexema + 2, tamanho)
            lexema_invalido = codigo[inicio_lexema:fim]
            erros.append(criar_erro(
                codigo_erro="UNTERMINATED_BLOCK_COMMENT",
                lexema=lexema_invalido,
                linha=linha,
                coluna=coluna,
            ))
            coluna += len(lexema_invalido)
            inicio_lexema = fim
            continue

        nome, lexema = casar_maximal_munch(codigo, inicio_lexema)

        if nome is None:
            # --- literal de char invalido ---------------------------------
            if codigo[inicio_lexema] == "'":
                fim_linha = codigo.find("\n", inicio_lexema)
                if fim_linha == -1:
                    fim_linha = tamanho

                # procura o apostrofo de fechamento na mesma linha,
                # pulando qualquer caractere escapado (\')
                pos_fechamento = None
                pos = inicio_lexema + 1
                while pos < fim_linha:
                    if codigo[pos] == "\\" and pos + 1 < fim_linha:
                        pos += 2
                        continue
                    if codigo[pos] == "'":
                        pos_fechamento = pos
                        break
                    pos += 1

                if pos_fechamento is None:
                    # nunca fechou na linha -> literal nao terminado.
                    # O erro cobre do apostrofo de abertura ate o primeiro
                    # caractere de resincronizacao ou o fim da linha, o que
                    # vier primeiro - mesma logica do comentario de bloco.
                    fim = encontrar_fim_recuperacao(codigo, inicio_lexema + 1, tamanho)
                    lexema_erro = codigo[inicio_lexema:fim]
                    erros.append(criar_erro(
                        codigo_erro="UNTERMINATED_CHAR_LITERAL",
                        lexema=lexema_erro,
                        linha=linha,
                        coluna=coluna,
                    ))
                    coluna += len(lexema_erro)
                    inicio_lexema = fim
                else:
                    # fechou, mas nao com exatamente 1 caractere:
                    # '' (vazio) ou 'ab' (mais de um caractere).
                    # Aqui o erro fica naturalmente limitado pelo apostrofo
                    # de fechamento encontrado, entao nao ha nada para
                    # engolir alem do proprio literal invalido.
                    lexema_erro = codigo[inicio_lexema:pos_fechamento + 1]
                    conteudo = lexema_erro[1:-1]
                    codigo_erro_char = (
                        "EMPTY_CHAR_LITERAL" if conteudo == ""
                        else "INVALID_CHAR_LITERAL"
                    )
                    erros.append(criar_erro(
                        codigo_erro=codigo_erro_char,
                        lexema=lexema_erro,
                        linha=linha,
                        coluna=coluna,
                    ))
                    coluna += len(lexema_erro)
                    inicio_lexema += len(lexema_erro)
                continue

            # --- literal de string invalido (nao terminada) ----------------
            # Mesma logica: o erro cobre da aspa de abertura ate o primeiro
            # caractere de resincronizacao (")]}") ou o fim da linha, o
            # que vier primeiro. Assim um ")" ou ";" que exista depois do
            # ponto onde a string deveria ter fechado, na mesma linha,
            # continua sendo reconhecido como token valido.
            if codigo[inicio_lexema] == '"':
                fim = encontrar_fim_recuperacao(codigo, inicio_lexema + 1, tamanho)
                lexema_erro = codigo[inicio_lexema:fim]
                erros.append(criar_erro(
                    codigo_erro="UNTERMINATED_STRING_LITERAL",
                    lexema=lexema_erro,
                    linha=linha,
                    coluna=coluna,
                ))
                coluna += len(lexema_erro)
                inicio_lexema = fim
                continue

            caractere_invalido = codigo[inicio_lexema]
            erros.append(criar_erro(
                codigo_erro="UNKNOWN_SYMBOL",
                lexema=caractere_invalido,
                linha=linha,
                coluna=coluna,
            ))

            if caractere_invalido == "\n":
                linha += 1
                coluna = 1
            else:
                coluna += 1

            avanco = inicio_lexema + 1
            inicio_lexema = avanco
            continue

        avanco = inicio_lexema + len(lexema)
        linha_token = linha
        coluna_token = coluna

        for caractere in lexema:
            if caractere == "\n":
                linha += 1
                coluna = 1
            else:
                coluna += 1

        if nome in ("espaco", "comentario", "comentario_bloco"):
            pass

        elif nome == "id":
            if lexema in PALAVRAS_RESERVADAS:
                tokens.append(criar_token(TOKEN_PALAVRA_RESERVADA[lexema], lexema, None,
                                           linha_token, coluna_token))
            else:
                tokens.append(criar_token(TOKEN_CONSTANTE["id"], lexema, lexema,
                                           linha_token, coluna_token))

        elif nome == "num_int":
            tokens.append(criar_token(TOKEN_CONSTANTE["num_int"], lexema, int(lexema),
                                       linha_token, coluna_token))

        elif nome == "num_real":
            tokens.append(criar_token(TOKEN_CONSTANTE["num_real"], lexema, float(lexema),
                                       linha_token, coluna_token))

        elif nome in ("string_lit", "char_lit"):
            # o atributo agora e o conteudo JA com os escapes processados
            # (\n -> quebra de linha real, \" -> aspas, \\ -> barra, etc.)
            conteudo_bruto = lexema[1:-1]
            conteudo_processado = processar_escapes(
                conteudo_bruto, linha_token, coluna_token + 1, erros
            )
            tokens.append(criar_token(TOKEN_CONSTANTE[nome], lexema, conteudo_processado,
                                       linha_token, coluna_token))

        else:
            tokens.append(criar_token(TOKEN_CONSTANTE[nome], lexema, None,
                                       linha_token, coluna_token))

        inicio_lexema = avanco

    tokens.append(criar_token("EOF", "", None, linha, coluna))

    return tokens, erros

# --- CLI / leitura de arquivo: contrato comum com scanner.c e parser.c ---------
# stdout: bytes UTF-8, cada linha terminada em "\n" (sem traducao de newline da plataforma).
# stderr: mensagens canonicas abaixo; exit code 1 em qualquer falha de execucao.
def escrever_stderr(texto_bytes):
    sys.stderr.buffer.write(texto_bytes)
    sys.stderr.buffer.flush()

def falhar(mensagem_bytes):
    escrever_stderr(mensagem_bytes)
    sys.exit(1)

def exigir_argumento(nome_programa):
    if len(sys.argv) < 2:
        falhar(("Uso: %s <arquivo>\n" % nome_programa).encode("utf-8"))
    return sys.argv[1]

def ler_codigo(caminho):
    """Le o arquivo, valida UTF-8 e aplica a normalizacao CRLF/CR -> LF."""
    try:
        with open(caminho, "rb") as arquivo:
            dados = arquivo.read()
    except OSError:
        falhar(b"Erro ao abrir arquivo: " + os.fsencode(caminho) + b"\n")
    try:
        texto = dados.decode("utf-8")
    except UnicodeDecodeError:
        falhar(b"Erro: entrada com UTF-8 invalido\n")
    return texto.replace("\r\n", "\n").replace("\r", "\n")

def escrever_stdout(texto):
    sys.stdout.buffer.write(texto.encode("utf-8"))
    sys.stdout.buffer.flush()

def main():
    caminho = exigir_argumento("scanner")
    codigo = ler_codigo(caminho)

    tokens, erros = scanner(codigo)

    saida = []
    for token in tokens:
        saida.append(token_para_texto(token) + "\n")
    for erro in erros:
        saida.append(erro_para_texto(erro) + "\n")
    escrever_stdout("".join(saida))

if __name__ == "__main__":
    main()
