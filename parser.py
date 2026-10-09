#!/usr/bin/env python
import json
import sys

import scanner as lexer

# Contrato comum com parser.c: limites de aninhamento (em vez de depender da pilha/recursao
# de cada runtime). Estourar qualquer um aborta com a mesma mensagem e exit code 1.
MAX_PROF_PARSE = 1000   # chamadas aninhadas de parse_comando/parse_atribuicao/parse_unario
MAX_PROF_AST = 10000    # profundidade de renderizar()
MSG_LIMITE = b"Erro: limite de aninhamento excedido\n"
sys.setrecursionlimit(400000)

class LimiteAninhamento(Exception):
    pass

#Construção de nós da AST
def no(node_tipo, linha=None, coluna=None, **campos):
    d = {"node": node_tipo}
    if linha is not None:
        d["linha"] = linha
    if coluna is not None:
        d["coluna"] = coluna
    d.update(campos)
    return d

class ErroSintatico(Exception):
    """Usado internamente para abortar a producao atual e acionar recuperacao."""
    pass

TIPOS = ("INT", "FLOAT", "BOOL", "CHAR", "VOID")

#Tokens em que é seguro retomar a análise após um erro
SYNC_DECL = TIPOS + ("EOF",)
SYNC_STATEMENT = TIPOS + (
    "SEMICOLON", "RBRACE", "EOF",
    "IF", "ELSE", "WHILE", "FOR", "RETURN", "BREAK", "CONTINUE",
    "PRINT", "READ", "LBRACE",
)

class Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.pos = 0
        self.erros = []
        self.prof = 0

    def entrar(self):
        self.prof += 1
        if self.prof > MAX_PROF_PARSE:
            raise LimiteAninhamento()

    #Infraestrutura
    def atual(self):
        return self.tokens[self.pos]

    def verifica(self, *tipos):
        return self.atual()["token"] in tipos

    def avancar(self):
        tok = self.tokens[self.pos]
        if tok["token"] != "EOF":
            self.pos += 1
        return tok

    def casar(self, *tipos):
        if self.verifica(*tipos):
            return self.avancar()
        return None

    def esperar(self, tipo, contexto):
        if self.verifica(tipo):
            return self.avancar()
        self.registrar_erro(contexto, esperado=tipo)
        raise ErroSintatico()

    def registrar_erro(self, contexto, esperado=None):
        tok = self.atual()
        achado = tok["lexeme"] if tok["lexeme"] else tok["token"]
        msg_esperado = f"; esperado {esperado}" if esperado else ""
        self.erros.append({
            "erro": "SYNTAX_ERROR",
            "lexema": tok["lexeme"],
            "linha": tok["line"],
            "coluna": tok["column"],
            "mensagem": f"token inesperado '{achado}'{msg_esperado}; contexto: {contexto}",
        })

    def sincronizar(self, tokens_sincronizacao):
        while not self.verifica("EOF"):
            if self.atual()["token"] in tokens_sincronizacao:
                return
            self.avancar()

    #Programa e Declarações globais
    def parse_programa(self):
        declaracoes = []
        while not self.verifica("EOF"):
            try:
                declaracoes.append(self.parse_declaracao_global())
            except ErroSintatico:
                self.sincronizar(SYNC_DECL)
        return no("Program", declaracoes=declaracoes)

    def parse_tipo(self):
        tok = self.atual()
        if tok["token"] in TIPOS:
            self.avancar()
            return tok["lexeme"]
        self.registrar_erro("tipo", esperado="tipo (int/float/bool/char/void)")
        raise ErroSintatico()

    def parse_declaracao_global(self):
        if not self.verifica(*TIPOS):
            return self.parse_comando()

        tok0 = self.atual()
        linha, coluna = tok0["line"], tok0["column"]
        tipo = self.parse_tipo()
        nome_tok = self.esperar("IDENT", "nome apos tipo")
        nome = nome_tok["lexeme"]

        if self.verifica("LPAREN"):
            return self.parse_funcao(tipo, nome, linha, coluna)
        return self.parse_var_resto(tipo, nome, linha, coluna)

    def parse_funcao(self, tipo, nome, linha, coluna):
        self.esperar("LPAREN", "abertura de parametros")
        params = []
        if not self.verifica("RPAREN"):
            params.append(self.parse_param())
            while self.casar("COMMA"):
                params.append(self.parse_param())
        self.esperar("RPAREN", "fechamento de parametros")
        corpo = self.parse_bloco()
        return no("Function", linha, coluna, tipo=tipo, nome=nome, params=params, corpo=corpo)

    def parse_param(self):
        tok = self.atual()
        tipo = self.parse_tipo()
        nome_tok = self.esperar("IDENT", "nome do parametro")
        vetor = False
        if self.casar("LBRACKET"):
            self.esperar("RBRACKET", "fechamento de parametro vetor")
            vetor = True
        return no("Param", tok["line"], tok["column"], tipo=tipo, nome=nome_tok["lexeme"], vetor=vetor)

    def parse_var_resto(self, tipo, nome, linha, coluna):
        tamanho = None
        valor = None

        if self.casar("LBRACKET"):
            tamanho = self.parse_expressao()
            self.esperar("RBRACKET", "fechamento de vetor")
        elif self.casar("ASSIGN"):
            valor = self.parse_expressao()

        self.esperar("SEMICOLON", "termino de declaracao")
        return no("VarDecl", linha, coluna, tipo=tipo, nome=nome, tamanho=tamanho, valor=valor)

    #Blocos e Declarações locais
    def parse_bloco(self):
        abre = self.esperar("LBRACE", "abertura de bloco")
        itens = []
        while not self.verifica("RBRACE", "EOF"):
            pos_antes = self.pos
            try:
                itens.append(self.parse_item_bloco())
            except ErroSintatico:
                self.sincronizar(SYNC_STATEMENT)
                if self.verifica("SEMICOLON"):
                    self.avancar()
                elif self.pos == pos_antes:
                    self.avancar()
        self.esperar("RBRACE", "fechamento de bloco")
        return no("Block", abre["line"], abre["column"], itens=itens)

    def parse_item_bloco(self):
        if self.verifica(*TIPOS):
            return self.parse_declaracao_local()
        return self.parse_comando()

    def parse_declaracao_local(self):
        tok0 = self.atual()
        tipo = self.parse_tipo()
        nome_tok = self.esperar("IDENT", "nome apos tipo")
        return self.parse_var_resto(tipo, nome_tok["lexeme"], tok0["line"], tok0["column"])

    #Comandos
    def parse_comando(self):
        self.entrar()
        try:
            if self.verifica("LBRACE"):
                return self.parse_bloco()
            if self.verifica("IF"):
                return self.parse_if()
            if self.verifica("WHILE"):
                return self.parse_while()
            if self.verifica("FOR"):
                return self.parse_for()
            if self.verifica("RETURN"):
                return self.parse_return()
            if self.verifica("BREAK"):
                return self.parse_break()
            if self.verifica("CONTINUE"):
                return self.parse_continue()
            if self.verifica("PRINT"):
                return self.parse_print()
            if self.verifica("READ"):
                return self.parse_read()
            return self.parse_comando_expr()
        finally:
            self.prof -= 1

    def parse_if(self):
        tok = self.avancar()  #'if'
        self.esperar("LPAREN", "condicao do if")
        condicao = self.parse_expressao()
        self.esperar("RPAREN", "fechamento da condicao do if")
        entao = self.parse_comando()
        senao = None
        if self.casar("ELSE"):
            senao = self.parse_comando()
        return no("If", tok["line"], tok["column"], condicao=condicao, entao=entao, senao=senao)

    def parse_while(self):
        tok = self.avancar()
        self.esperar("LPAREN", "condicao do while")
        condicao = self.parse_expressao()
        self.esperar("RPAREN", "fechamento da condicao do while")
        corpo = self.parse_comando()
        return no("While", tok["line"], tok["column"], condicao=condicao, corpo=corpo)

    def parse_for(self):
        tok = self.avancar()  #'for'
        self.esperar("LPAREN", "abertura do for")

        init = None
        if self.verifica(*TIPOS):
            tok_i = self.atual()
            tipo = self.parse_tipo()
            nome_tok = self.esperar("IDENT", "nome no init do for")
            valor = None
            if self.casar("ASSIGN"):
                valor = self.parse_expressao()
            init = no("VarDecl", tok_i["line"], tok_i["column"],
                       tipo=tipo, nome=nome_tok["lexeme"], tamanho=None, valor=valor)
        elif not self.verifica("SEMICOLON"):
            tok_e = self.atual()
            init = no("ExprStmt", tok_e["line"], tok_e["column"], expressao=self.parse_expressao())
        self.esperar("SEMICOLON", "separador do for (apos inicializacao)")

        condicao = None
        if not self.verifica("SEMICOLON"):
            condicao = self.parse_expressao()
        self.esperar("SEMICOLON", "separador do for (apos condicao)")

        incremento = None
        if not self.verifica("RPAREN"):
            incremento = self.parse_expressao()
        self.esperar("RPAREN", "fechamento do for")

        corpo = self.parse_comando()
        return no("For", tok["line"], tok["column"],
                  init=init, condicao=condicao, incremento=incremento, corpo=corpo)

    def parse_return(self):
        tok = self.avancar()
        valor = None
        if not self.verifica("SEMICOLON"):
            valor = self.parse_expressao()
        self.esperar("SEMICOLON", "termino do return")
        return no("Return", tok["line"], tok["column"], valor=valor)

    def parse_break(self):
        tok = self.avancar()
        self.esperar("SEMICOLON", "termino do break")
        return no("Break", tok["line"], tok["column"])

    def parse_continue(self):
        tok = self.avancar()
        self.esperar("SEMICOLON", "termino do continue")
        return no("Continue", tok["line"], tok["column"])

    def parse_print(self):
        tok = self.avancar()
        self.esperar("LPAREN", "abertura do print")
        valor = self.parse_expressao()
        self.esperar("RPAREN", "fechamento do print")
        self.esperar("SEMICOLON", "termino do print")
        return no("Print", tok["line"], tok["column"], valor=valor)

    def parse_read(self):
        tok = self.avancar()
        self.esperar("LPAREN", "abertura do read")
        alvo = self.parse_posfixo()
        if alvo["node"] not in ("Id", "Index"):
            self.registrar_erro("alvo do read precisa ser uma variavel ou posicao de vetor")
        self.esperar("RPAREN", "fechamento do read")
        self.esperar("SEMICOLON", "termino do read")
        return no("Read", tok["line"], tok["column"], alvo=alvo)

    def parse_comando_expr(self):
        tok = self.atual()
        if self.casar("SEMICOLON"):
            return no("ExprStmt", tok["line"], tok["column"], expressao=None)
        expr = self.parse_expressao()
        self.esperar("SEMICOLON", "termino da expressao")
        return no("ExprStmt", tok["line"], tok["column"], expressao=expr)

    #Expressões
    def parse_expressao(self):
        return self.parse_atribuicao()

    def parse_atribuicao(self):
        self.entrar()
        try:
            esquerda = self.parse_ou_logico()
            if self.verifica("ASSIGN"):
                tok = self.avancar()
                direita = self.parse_atribuicao()
                return no("Assign", esquerda.get("linha"), esquerda.get("coluna"), alvo=esquerda, valor=direita)
            return esquerda
        finally:
            self.prof -= 1

    def _binario_esquerda(self, proximo_nivel, operadores):
        resultado = proximo_nivel()
        while self.verifica(*operadores):
            tok = self.avancar()
            direita = proximo_nivel()
            resultado = no("Binary", tok["line"], tok["column"],
                           op=tok["lexeme"], esquerda=resultado, direita=direita)
        return resultado

    def parse_ou_logico(self):
        return self._binario_esquerda(self.parse_e_logico, ("OR",))

    def parse_e_logico(self):
        return self._binario_esquerda(self.parse_igualdade, ("AND",))

    def parse_igualdade(self):
        return self._binario_esquerda(self.parse_relacional, ("EQ", "NEQ"))

    def parse_relacional(self):
        return self._binario_esquerda(self.parse_aditiva, ("LT", "LE", "GT", "GE"))

    def parse_aditiva(self):
        return self._binario_esquerda(self.parse_multiplicativa, ("PLUS", "MINUS"))

    def parse_multiplicativa(self):
        return self._binario_esquerda(self.parse_unario, ("STAR", "SLASH", "PERCENT"))

    def parse_unario(self):
        self.entrar()
        try:
            if self.verifica("MINUS", "NOT"):
                tok = self.avancar()
                operando = self.parse_unario()
                return no("Unary", tok["line"], tok["column"], op=tok["lexeme"], operando=operando)
            return self.parse_posfixo()
        finally:
            self.prof -= 1

    def parse_posfixo(self):
        expr = self.parse_primario()
        while True:
            if self.verifica("LPAREN"):
                tok = self.avancar()
                argumentos = []
                if not self.verifica("RPAREN"):
                    argumentos.append(self.parse_expressao())
                    while self.casar("COMMA"):
                        argumentos.append(self.parse_expressao())
                self.esperar("RPAREN", "fechamento de chamada")
                expr = no("Call", tok["line"], tok["column"], callee=expr, argumentos=argumentos)
            elif self.verifica("LBRACKET"):
                tok = self.avancar()
                indice = self.parse_expressao()
                self.esperar("RBRACKET", "fechamento de indice")
                expr = no("Index", tok["line"], tok["column"], base=expr, indice=indice)
            else:
                break
        return expr

    def parse_primario(self):
        tok = self.atual()

        if self.casar("IDENT"):
            return no("Id", tok["line"], tok["column"], nome=tok["lexeme"])
        if self.casar("INT_LIT"):
            return no("Lit", tok["line"], tok["column"], tipo="int", valor=tok["attribute"])
        if self.casar("FLOAT_LIT"):
            return no("Lit", tok["line"], tok["column"], tipo="float", valor=tok["attribute"])
        if self.casar("STRING_LIT"):
            return no("Lit", tok["line"], tok["column"], tipo="string", valor=tok["attribute"])
        if self.casar("CHAR_LIT"):
            return no("Lit", tok["line"], tok["column"], tipo="char", valor=tok["attribute"])
        if self.casar("TRUE"):
            return no("Lit", tok["line"], tok["column"], tipo="bool", valor=True)
        if self.casar("FALSE"):
            return no("Lit", tok["line"], tok["column"], tipo="bool", valor=False)
        if self.casar("LPAREN"):
            expr = self.parse_expressao()
            self.esperar("RPAREN", "fechamento de agrupamento")
            return expr

        self.registrar_erro("expressao", esperado="expressão")
        raise ErroSintatico()

def analisar(codigo):
    tokens, erros_lexicos = lexer.scanner(codigo)
    parser = Parser(tokens)
    ast = parser.parse_programa()
    return ast, erros_lexicos, parser.erros

#Renderização da AST em texto
_prof_render = 0

def renderizar(node):
    global _prof_render
    if node is None:
        return "NULL"
    _prof_render += 1
    if _prof_render > MAX_PROF_AST:
        raise LimiteAninhamento()
    try:
        return _renderizar_no(node)
    finally:
        _prof_render -= 1

def _renderizar_no(node):

    tipo = node["node"]

    if tipo == "Program":
        return "Program(" + ", ".join(renderizar(d) for d in node["declaracoes"]) + ")"

    if tipo == "Function":
        params = ",".join(f'{p["tipo"]} {p["nome"]}' for p in node["params"])
        return f'Function({node["tipo"]} {node["nome"]}({params}) {renderizar(node["corpo"])})'

    if tipo == "Block":
        return "Block(" + ", ".join(renderizar(i) for i in node["itens"]) + ")"

    if tipo == "VarDecl":
        base = f'{node["tipo"]} {node["nome"]}'
        if node.get("tamanho") is not None:
            base += f' size={renderizar(node["tamanho"])}'
        elif node.get("valor") is not None:
            base += f'={renderizar(node["valor"])}'
        return f'VarDecl({base})'

    if tipo == "If":
        partes = [renderizar(node["condicao"]), renderizar(node["entao"]), renderizar(node.get("senao"))]
        return "If(" + ",".join(partes) + ")"

    if tipo == "While":
        return f'While({renderizar(node["condicao"])},{renderizar(node["corpo"])})'

    if tipo == "For":
        return (f'For({renderizar(node.get("init"))},{renderizar(node.get("condicao"))},'
                f'{renderizar(node.get("incremento"))},{renderizar(node["corpo"])})')

    if tipo == "Return":
        return f'Return({renderizar(node.get("valor"))})'

    if tipo == "Break":
        return "Break()"

    if tipo == "Continue":
        return "Continue()"

    if tipo == "Print":
        return f'Print({renderizar(node["valor"])})'

    if tipo == "Read":
        return f'Read({renderizar(node["alvo"])})'

    if tipo == "ExprStmt":
        return f'ExprStmt({renderizar(node.get("expressao"))})'

    if tipo == "Assign":
        return f'Assign({renderizar(node["alvo"])},{renderizar(node["valor"])})'

    if tipo == "Binary":
        return f'Binary({node["op"]},{renderizar(node["esquerda"])},{renderizar(node["direita"])})'

    if tipo == "Unary":
        return f'Unary({node["op"]},{renderizar(node["operando"])})'

    if tipo == "Call":
        args = ",".join(renderizar(a) for a in node["argumentos"])
        callee = renderizar(node["callee"])
        return f'Call({callee}{"," + args if args else ""})'

    if tipo == "Index":
        return f'Index({renderizar(node["base"])},{renderizar(node["indice"])})'

    if tipo == "Id":
        return f'Id({node["nome"]})'

    if tipo == "Lit":
        valor = node["valor"]
        subtipo = node["tipo"]
        if subtipo == "float":
            subtipo = "real"

        if subtipo == "string":
            valor_txt = json.dumps(valor, ensure_ascii=False)
        elif subtipo == "char":
            valor_txt = "'" + str(valor).replace("'", "\\'") + "'"
        elif subtipo == "bool":
            valor_txt = "true" if valor else "false"
        else:
            valor_txt = str(valor)
        return f'Lit({subtipo},{valor_txt})'

    return f'{tipo}(?)'

MSG_REJEITADO = "NÃO HÁ AST: o parser deve rejeitar a entrada."

def formatar_resultado(codigo):
    ast, erros_lexicos, erros_sintaticos = analisar(codigo)

    if erros_lexicos or erros_sintaticos:
        return MSG_REJEITADO

    return renderizar(ast)

def main():
    caminho = lexer.exigir_argumento("parser")
    codigo = lexer.ler_codigo(caminho)
    try:
        saida = formatar_resultado(codigo)
    except LimiteAninhamento:
        lexer.falhar(MSG_LIMITE)
    lexer.escrever_stdout(saida + "\n")

if __name__ == "__main__":
    main()
