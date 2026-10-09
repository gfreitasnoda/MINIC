#!/usr/bin/env python
import json
import sys
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Tuple

#Tipos, categorias e fluxo
INT, FLOAT, BOOL, CHAR, VOID, STRING = "int", "float", "bool", "char", "void", "string"
ERRO = "<erro>"
NUMERICOS = (INT, FLOAT)
TIPOS_VARIAVEL = (INT, FLOAT, BOOL, CHAR)

CAT_VARIAVEL = "variavel"
CAT_PARAMETRO = "parametro"
CAT_VETOR = "vetor"
CAT_FUNCAO = "funcao"
_DESC_CATEGORIA = {
    CAT_VARIAVEL: "uma variável", CAT_PARAMETRO: "um parâmetro",
    CAT_VETOR: "um vetor", CAT_FUNCAO: "uma função",
}

RETORNA = "RETORNA"
PODE_CONTINUAR = "PODE_CONTINUAR"

CATEGORIAS_DIAGNOSTICO = {
    "SEM001": "não declarado", "SEM002": "duplicado", "SEM003": "incompatível",
    "SEM004": "operador", "SEM005": "condição", "SEM006": "índice",
    "SEM007": "chamada", "SEM008": "argumento", "SEM009": "retorno",
    "SEM010": "contexto", "SEM011": "queda", "SEM012": "void",
    "SEM013": "destino não atribuível",
}

#Diagnosticos
@dataclass
class Diagnostico:
    codigo: str
    categoria: str
    linha: Optional[int]
    coluna: Optional[int]
    mensagem: str
    nome: Optional[str] = None
    operador: Optional[str] = None
    esperado: Optional[str] = None
    recebido: Optional[str] = None
    decl_linha: Optional[int] = None
    decl_coluna: Optional[int] = None

    def __str__(self):
        return f"{self.codigo} — linha {self.linha}, coluna {self.coluna}: {self.mensagem}"

#Tabela de simbolos e escopos
@dataclass(eq=False)
class Simbolo:
    nome: str
    categoria: str
    tipo: str
    parametros: Optional[List[Tuple[str, str]]] = None
    tipo_retorno: Optional[str] = None
    no: Any = field(default=None, repr=False)
    linha: Optional[int] = None
    coluna: Optional[int] = None
    visivel_desde: int = 0
    tamanho_const: Optional[int] = None

    def resumo(self):
        d = {"nome": self.nome, "categoria": self.categoria, "tipo": self.tipo,
             "linha": self.linha, "coluna": self.coluna}
        if self.categoria == CAT_FUNCAO:
            d["parametros"] = [list(p) for p in self.parametros]
            d["tipo_retorno"] = self.tipo_retorno
        return d

class Escopo:
    def __init__(self, pai: Optional["Escopo"] = None, nome: str = "bloco"):
        self.pai = pai
        self.nome = nome
        self.simbolos: Dict[str, Simbolo] = {}

    def declarar(self, simbolo: Simbolo):
        self.simbolos[simbolo.nome] = simbolo

    def buscar_local(self, nome: str) -> Optional[Simbolo]:
        return self.simbolos.get(nome)

    def buscar(self, nome: str, ordem: Optional[int] = None) -> Optional[Simbolo]:
        esc = self
        while esc is not None:
            s = esc.simbolos.get(nome)
            if s is not None and (ordem is None or s.visivel_desde <= ordem):
                return s
            esc = esc.pai
        return None

    def buscar_qualquer(self, nome: str) -> Optional[Simbolo]:
        esc = self
        while esc is not None:
            if nome in esc.simbolos:
                return esc.simbolos[nome]
            esc = esc.pai
        return None

@dataclass
class FunctionContext:
    nome: Optional[str]
    tipo_retorno: Optional[str]
    parametros: List[Tuple[str, str]] = field(default_factory=list)
    loop_depth: int = 0

@dataclass
class ResultadoSemantico:
    diagnosticos: List[Diagnostico]
    ast: dict
    escopo_global: Escopo

    @property
    def sucesso(self) -> bool:
        return not self.diagnosticos

    @property
    def status(self) -> str:
        return "sucesso" if self.sucesso else "falha"

#Utilitarios
def _inicio(n):
    k = n["node"]
    if k == "Binary":
        return _inicio(n["esquerda"])
    if k == "Call":
        return _inicio(n["callee"])
    if k == "Index":
        return _inicio(n["base"])
    if k == "Assign":
        return _inicio(n["alvo"])
    return n.get("linha"), n.get("coluna")

def _const_int(n):
    if n is None:
        return None
    if n["node"] == "Lit" and n["tipo"] == INT:
        return n["valor"]
    if n["node"] == "Unary" and n["op"] == "-":
        v = _const_int(n["operando"])
        return -v if v is not None else None
    return None

def _compativel(destino, origem):
    if destino == origem and (destino in TIPOS_VARIAVEL
                              or (destino.endswith("[]") and destino[:-2] in TIPOS_VARIAVEL)):
        return True, None
    if destino == FLOAT and origem == INT:
        return True, "int->float"
    return False, None

def _plural(n, palavra):
    return f"{n} {palavra}" + ("" if n == 1 else "s")

def _assinatura(sym):
    return "(" + ",".join(t for t, _ in sym.parametros) + ")->" + sym.tipo_retorno

def _texto_expr(n):
    if not isinstance(n, dict):
        return ""
    k = n.get("node")
    if k == "Lit":
        v = n.get("valor")
        if isinstance(v, bool): return "true" if v else "false"
        return str(v)
    if k == "Id": return str(n.get("nome", ""))
    if k == "Binary": return f"{_texto_expr(n.get('esquerda'))} {n.get('op', '')} {_texto_expr(n.get('direita'))}".strip()
    if k == "Unary": return f"{n.get('op', '')}{_texto_expr(n.get('operando'))}"
    if k == "Assign": return f"{_texto_expr(n.get('alvo'))} = {_texto_expr(n.get('valor'))}"
    if k == "Index": return f"{_texto_expr(n.get('base'))}[{_texto_expr(n.get('indice'))}]"
    if k == "Call": return f"{_texto_expr(n.get('callee'))}({', '.join(_texto_expr(a) for a in n.get('argumentos', []))})"
    return ""

#Analisador
class SemanticAnalyzer:
    def __init__(self, ast, anotar=True):
        self.ast = ast
        self.anotar = anotar
        self._reiniciar()

    def _reiniciar(self):
        self.diagnosticos: List[Diagnostico] = []
        self.global_scope = Escopo(None, "global")
        self.escopo = self.global_scope
        self.ctx = FunctionContext(None, None)
        self.ordem_atual = 0
        self._decl_atual = None
        self._simbolo_de: Dict[int, Simbolo] = {}
        self._seq = 0

    def analyze(self) -> ResultadoSemantico:
        self._reiniciar()
        decls = self.ast.get("declaracoes", [])

        #Fase 1: coletar declaracoes globais (funcoes e variaveis)
        self._coletar_globais(decls)

        #Fase 2: analisar, em ordem, cada item de nivel superior
        for idx, d in enumerate(decls):
            self.ordem_atual = idx
            self.ctx = FunctionContext(None, None)
            self._decl_atual = None
            k = d["node"]
            if k == "Function":
                self._analisar_funcao(d, self._simbolo_de[id(d)])
            elif k == "VarDecl":
                self._decl_atual = self._simbolo_de[id(d)]
                self._analisar_inicializacao(d, self._simbolo_de[id(d)])
            else:
                self._stmt(d)

        self._sem(self.ast)["simbolos_globais"] = [
            s.resumo() for s in self.global_scope.simbolos.values()]
        diags = sorted(
            self.diagnosticos,
            key=lambda x: (x.linha or 0, x.coluna or 0, x._seq))
        return ResultadoSemantico(diags, self.ast, self.global_scope)

    def _erro(self, codigo, pos, mensagem, **extras):
        linha, coluna = pos
        d = Diagnostico(codigo, CATEGORIAS_DIAGNOSTICO[codigo], linha, coluna,
                        mensagem, **extras)
        self._seq += 1
        d._seq = self._seq
        self.diagnosticos.append(d)

    def _sem(self, n):
        return n.setdefault("sem", {}) if self.anotar else {}

    def _abrir(self, nome="bloco"):
        self.escopo = Escopo(self.escopo, nome)

    def _fechar(self):
        self.escopo = self.escopo.pai

    def _declarar(self, sym) -> bool:
        anterior = self.escopo.buscar_local(sym.nome)
        if anterior is not None:
            col_nome = sym.coluna + len(sym.no.get("tipo", "")) + 1
            decl_col_nome = anterior.coluna + len(anterior.no.get("tipo", "")) + 1
            self._erro(
                "SEM002", (sym.linha, col_nome),
                f'“{sym.nome}” já declarado neste escopo; declaração anterior '
                f'na linha {anterior.linha}, coluna {decl_col_nome} (tipo {anterior.tipo}).',
                nome=sym.nome, decl_linha=anterior.linha,
                decl_coluna=decl_col_nome)
            return False
        self.escopo.declarar(sym)
        return True

    def _simbolo_var(self, d, parametro=False, visivel_desde=0):
        tipo = d["tipo"]
        if tipo == VOID:
            self._erro(
                "SEM003", (d.get("linha"), d.get("coluna")),
                f'{"Parâmetro" if parametro else "Variável"} "{d["nome"]}" '
                f'não pode ter tipo void.', nome=d["nome"], recebido=VOID)
            tipo = ERRO
        if parametro:
            cat = CAT_VETOR if d.get("vetor", False) else CAT_PARAMETRO
        elif d.get("tamanho") is not None:
            cat = CAT_VETOR
        else:
            cat = CAT_VARIAVEL
        sym = Simbolo(d["nome"], cat, tipo, no=d, linha=d.get("linha"),
                      coluna=d.get("coluna"), visivel_desde=visivel_desde)
        if cat == CAT_VETOR:
            sym.tamanho_const = _const_int(d.get("tamanho"))
        return sym

    def _simbolo_funcao(self, d, visivel_desde):
        params = []
        for p in d["params"]:
            tp = ERRO if p["tipo"] == VOID else p["tipo"]
            if tp != ERRO and p.get("vetor", False):
                tp = f"{tp}[]"
            params.append((tp, p["nome"]))
        return Simbolo(d["nome"], CAT_FUNCAO, d["tipo"], parametros=params,
                       tipo_retorno=d["tipo"], no=d, linha=d.get("linha"),
                       coluna=d.get("coluna"), visivel_desde=visivel_desde)

    def _coletar_globais(self, decls):
        for idx, d in enumerate(decls):
            k = d["node"]
            if k == "Function":
                sym = self._simbolo_funcao(d, visivel_desde=idx)
            elif k == "VarDecl":
                sym = self._simbolo_var(d, visivel_desde=idx + 1)
            else:
                continue
            self._simbolo_de[id(d)] = sym
            self._declarar(sym)

    def _analisar_inicializacao(self, d, sym):
        sem = self._sem(d)
        sem["simbolo"] = sym
        sem["tipo_declarado"] = sym.tipo
        if d.get("tamanho") is not None:
            ts = self._expr(d["tamanho"])
            if ts not in (ERRO, INT):
                self._erro("SEM006", _inicio(d["tamanho"]),
                           f"Tamanho do vetor deve ser int; recebeu {ts}.",
                           nome=d["nome"], esperado=INT, recebido=ts)
            elif sym.tamanho_const is not None and sym.tamanho_const <= 0:
                self._erro("SEM006", _inicio(d["tamanho"]),
                           f'Tamanho do vetor "{d["nome"]}" deve ser positivo; '
                           f'recebeu {sym.tamanho_const}.', nome=d["nome"])
        if d.get("valor") is not None:
            tv = self._expr(d["valor"])
            sem["tipo_valor"] = tv
            if tv != ERRO and sym.tipo != ERRO:
                ok, conv = _compativel(sym.tipo, tv)
                sem["compativel"] = ok
                sem["conversao"] = conv
                if not ok:
                    self._erro(
                        "SEM003", _inicio(d["valor"]),
                        f'Não é possível atribuir {tv} a {sym.tipo} sem conversão '
                        f'permitida (destino “{d["nome"]}”; expressão '
                        f'“{d["valor"].get("valor", "")}”).',
                        nome=d["nome"], esperado=sym.tipo, recebido=tv)

    def _declaracao_local(self, d):
        sym = self._simbolo_var(d)
        self._analisar_inicializacao(d, sym)
        self._declarar(sym)
        return PODE_CONTINUAR

    def _analisar_funcao(self, d, sym):
        sem = self._sem(d)
        sem["simbolo"] = sym
        sem["assinatura"] = _assinatura(sym)

        self.ctx = FunctionContext(d["nome"], d["tipo"],
                                   list(sym.parametros), loop_depth=0)
        self._abrir(f"funcao {d['nome']}")
        for p in d["params"]:
            psym = self._simbolo_var(p, parametro=True)
            self._sem(p)["simbolo"] = psym
            self._declarar(psym)

        # parametros e corpo compartilham o escopo (como em C)
        fluxo = self._itens(d["corpo"]["itens"])
        if d["tipo"] != VOID and fluxo != RETORNA:
            cond_nome = "condicao"
            def _achar_cond(n):
                if isinstance(n, dict):
                    if n.get("node") == "If" and n.get("condicao", {}).get("node") == "Id":
                        return n["condicao"].get("nome")
                    for v in n.values():
                        r = _achar_cond(v)
                        if r:
                            return r
                elif isinstance(n, list):
                    for v in n:
                        r = _achar_cond(v)
                        if r:
                            return r
                return None
            cond_nome = _achar_cond(d["corpo"]) or cond_nome
            self._erro("SEM011", (d.get("linha"), d.get("coluna")),
                       f'A função “{d["nome"]}” pode terminar sem retornar {d["tipo"]}; '
                       f'o ramo em que “{cond_nome}” é falso alcança o fim do corpo.',
                       nome=d["nome"], esperado=d["tipo"])
        self._fechar()
        self.ctx = FunctionContext(None, None)

    def _itens(self, itens):
        fluxo = PODE_CONTINUAR
        for it in itens:
            if self._stmt(it) == RETORNA:
                fluxo = RETORNA
        return fluxo

    def _stmt(self, n):
        k = n["node"]
        if k == "VarDecl":
            return self._declaracao_local(n)
        if k == "Block":
            self._abrir()
            fluxo = self._itens(n["itens"])
            self._fechar()
            return fluxo
        if k == "ExprStmt":
            if n.get("expressao") is not None:
                self._expr(n["expressao"], permitir_void=True)
            return PODE_CONTINUAR
        if k == "If":
            self._condicao(n["condicao"], n)
            f1 = self._stmt(n["entao"])
            if n.get("senao") is not None:
                f2 = self._stmt(n["senao"])
                return RETORNA if (f1 == RETORNA and f2 == RETORNA) else PODE_CONTINUAR
            return PODE_CONTINUAR
        if k == "While":
            self._condicao(n["condicao"], n)
            self._corpo_laco(n)
            return PODE_CONTINUAR
        if k == "For":
            self._abrir("for")
            init = n.get("init")
            if init is not None:
                self._stmt(init)
            if n.get("condicao") is not None:
                self._condicao(n["condicao"], n)
            if n.get("incremento") is not None:
                self._expr(n["incremento"], permitir_void=True)
            self._corpo_laco(n)
            self._fechar()
            return PODE_CONTINUAR
        if k == "Return":
            return self._return(n)
        if k in ("Break", "Continue"):
            self._sem(n)["loop_depth"] = self.ctx.loop_depth
            if self.ctx.loop_depth == 0:
                palavra = "break" if k == "Break" else "continue"
                self._erro("SEM010", (n.get("linha"), n.get("coluna")),
                           f"{palavra} só pode ocorrer dentro de um laço.",
                           nome=palavra)
            return PODE_CONTINUAR
        if k == "Print":
            self._expr(n["valor"])
            return PODE_CONTINUAR
        if k == "Read":
            t, _ = self._destino(n["alvo"], "leitura")
            self._sem(n)["tipo_alvo"] = t
            return PODE_CONTINUAR
        if k == "Function":
            return PODE_CONTINUAR
        return PODE_CONTINUAR

    def _corpo_laco(self, n):
        self.ctx.loop_depth += 1
        self._stmt(n["corpo"])
        self.ctx.loop_depth -= 1

    def _condicao(self, expr, pai):
        t = self._expr(expr)
        self._sem(pai)["tipo_condicao"] = t
        if t not in (ERRO, BOOL):
            nome_cond = {"If": "if", "While": "while", "For": "for"}.get(
                pai.get("node"), "comando")
            expr_txt = expr.get("nome", expr.get("valor", ""))
            self._erro("SEM005", _inicio(expr),
                       f'Condição de {nome_cond} deve ter tipo bool; recebeu {t} '
                       f'(expressão “{expr_txt}”).',
                       esperado=BOOL, recebido=t)

    def _return(self, n):
        pos = (n.get("linha"), n.get("coluna"))
        valor = n.get("valor")
        tv = self._expr(valor) if valor is not None else None
        ctx = self.ctx
        sem = self._sem(n)
        sem["tipo_esperado"] = ctx.tipo_retorno
        sem["tipo_efetivo"] = VOID if valor is None else tv

        if ctx.nome is None:
            self._erro("SEM010", pos, "return só pode ocorrer dentro de uma função.",
                       nome="return")
            return PODE_CONTINUAR
        esperado = ctx.tipo_retorno
        if esperado == VOID:
            if valor is not None and tv != ERRO:
                self._erro("SEM009", pos,
                           f'Função void "{ctx.nome}" não pode retornar valor '
                           f'(recebeu {tv}).', nome=ctx.nome,
                           esperado=VOID, recebido=tv)
        elif valor is None:
            self._erro("SEM009", pos,
                       f'Retorno sem valor em função "{ctx.nome}" com tipo {esperado}.',
                       nome=ctx.nome, esperado=esperado, recebido=VOID)
        elif tv != ERRO:
            ok, conv = _compativel(esperado, tv)
            sem["conversao"] = conv
            if not ok:
                self._erro("SEM009", _inicio(valor),
                           f'Retorno {tv} incompatível com o tipo {esperado} da '
                           f'função “{ctx.nome}”; conversão implícita de {tv} para '
                           f'{esperado} não permitida.',
                           nome=ctx.nome, esperado=esperado, recebido=tv)
        return RETORNA

    def _expr(self, n, permitir_void=False):
        k = n["node"]
        handler = {
            "Lit": self._lit, "Id": self._id, "Binary": self._binary,
            "Unary": self._unary, "Assign": self._assign, "Index": self._index,
            "Call": self._call,
        }.get(k)
        t = handler(n) if handler else ERRO
        if t == VOID and not permitir_void:
            nome = n["callee"].get("nome") if k == "Call" else None
            self._erro("SEM012", _inicio(n),
                       f'Função “{nome}” não produz valor (retorno void) e não pode ser usada como expressão de atribuição.'
                       if nome else "Função não produz valor (retorno void) e não pode ser usada como expressão de atribuição.",
                       nome=nome)
            t = ERRO
        self._sem(n)["tipo"] = t
        return t

    def _lit(self, n):
        self._sem(n)["valor"] = n["valor"]
        return n["tipo"]

#Identificadores
    def _resolver(self, n) -> Optional[Simbolo]:
        nome = n["nome"]
        sym = self.escopo.buscar(nome, self.ordem_atual)
        sem = self._sem(n)
        if sym is None:
            outro = self.escopo.buscar_qualquer(nome)
            if outro is not None and outro is self._decl_atual:
                msg = f'Identificador "{nome}" não pode ser usado na própria inicialização.'
            elif outro is not None:
                msg = (f'Identificador "{nome}" não declarado neste escopo '
                       f'(declarado somente mais adiante, na linha {outro.linha}).')
            else:
                msg = f'Identificador “{nome}” não declarado neste escopo.'
            self._erro("SEM001", (n.get("linha"), n.get("coluna")), msg, nome=nome)
            sem.update(simbolo=None, categoria=None, tipo=ERRO)
            return None
        tipo = sym.tipo
        if sym.categoria == CAT_VETOR:
            tipo = f"{sym.tipo}[]"
        elif sym.categoria == CAT_FUNCAO:
            tipo = _assinatura(sym)
        sem.update(simbolo=sym, categoria=sym.categoria, tipo=tipo)
        return sym

    def _id(self, n):
        sym = self._resolver(n)
        if sym is None:
            return ERRO
        pos = (n.get("linha"), n.get("coluna"))
        if sym.categoria == CAT_VETOR:
            self._erro("SEM003", pos,
                       f'"{sym.nome}" é um vetor e não pode ser usado como valor; '
                       f'use {sym.nome}[i] para acessar um elemento.', nome=sym.nome)
            return ERRO
        if sym.categoria == CAT_FUNCAO:
            self._erro("SEM003", pos,
                       f'"{sym.nome}" é uma função e não pode ser usada como valor; '
                       f'use {sym.nome}(...) para chamá-la.', nome=sym.nome)
            return ERRO
        return sym.tipo

#Operadores
    def _binary(self, n):
        op = n["op"]
        te = self._expr(n["esquerda"])
        td = self._expr(n["direita"])
        sem = self._sem(n)
        sem.update(tipo_esquerda=te, tipo_direita=td, conversoes=[])
        if te == ERRO or td == ERRO:
            return ERRO

        resultado, exigencia = None, ""
        numericos = te in NUMERICOS and td in NUMERICOS
        if op in ("+", "-", "*", "/"):
            exigencia = "numéricos"
            if numericos:
                resultado = FLOAT if FLOAT in (te, td) else INT
        elif op == "%":
            exigencia = "int"
            if te == INT and td == INT:
                resultado = INT
        elif op in ("<", "<=", ">", ">="):
            exigencia = "numéricos (ou ambos char)"
            if numericos or (te == CHAR and td == CHAR):
                resultado = BOOL
        elif op in ("==", "!="):
            exigencia = "numéricos ou do mesmo tipo (bool/char)"
            if numericos or (te == td and te in (BOOL, CHAR)):
                resultado = BOOL
        elif op in ("&&", "||"):
            exigencia = "bool"
            if te == BOOL and td == BOOL:
                resultado = BOOL

        if resultado is None:
            self._erro("SEM004", (n.get("linha"), n.get("coluna")),
                       f"Operador {op} requer operandos {exigencia}; "
                       f"recebeu {te} e {td}.", operador=op,
                       esperado=exigencia, recebido=f"{te}, {td}")
            return ERRO
        if numericos and te != td:
            sem["conversoes"].append("esquerda int->float" if te == INT
                                     else "direita int->float")
        return resultado

    def _unary(self, n):
        op = n["op"]
        t = self._expr(n["operando"])
        self._sem(n).update(tipo_operando=t, conversoes=[])
        if t == ERRO:
            return ERRO
        if op == "-" and t in NUMERICOS:
            return t
        if op == "!" and t == BOOL:
            return BOOL
        exigencia = "numérico" if op == "-" else "bool"
        self._erro("SEM004", (n.get("linha"), n.get("coluna")),
                   f"Operador {op} requer operando {exigencia}; recebeu {t}.",
                   operador=op, esperado=exigencia, recebido=t)
        return ERRO

#Atribuicao
    def _destino(self, alvo, acao="atribuição"):
        k = alvo["node"]
        pos = _inicio(alvo)
        if k == "Id":
            sym = self._resolver(alvo)
            if sym is None:
                return ERRO, False
            if sym.categoria == CAT_VETOR:
                self._erro("SEM003", pos,
                           f'Não é possível usar o vetor "{sym.nome}" inteiro como '
                           f'destino de {acao}; use {sym.nome}[i].', nome=sym.nome)
                return ERRO, False
            if sym.categoria == CAT_FUNCAO:
                self._erro("SEM003", pos,
                           f'"{sym.nome}" é uma função e não pode ser destino de '
                           f'{acao}.', nome=sym.nome)
                return ERRO, False
            return sym.tipo, True
        if k == "Index":
            return self._expr(alvo), True
        self._expr(alvo, permitir_void=True)
        descricao = {"Lit": "literal", "Call": "chamada de função",
                     "Binary": "expressão", "Unary": "expressão"}.get(k, "expressão")
        if acao == "atribuição" and k == "Lit" and alvo.get("tipo") == INT:
            codigo = "SEM013"
            mensagem = ('Destino de atribuição não é atribuível; o literal inteiro '
                        f'“{alvo.get("valor")}” não designa uma variável ou elemento de vetor.')
        else:
            codigo = "SEM003"
            mensagem = f"Destino de {acao} inválido: {descricao} não é atribuível."
        self._erro(codigo, pos, mensagem)
        return ERRO, False

    def _assign(self, n):
        td, atribuivel = self._destino(n["alvo"])
        tv = self._expr(n["valor"])
        sem = self._sem(n)
        sem.update(tipo_destino=td, tipo_valor=tv, atribuivel=atribuivel,
                   compativel=None, conversao=None)
        if td == ERRO or tv == ERRO:
            return td
        ok, conv = _compativel(td, tv)
        sem["compativel"], sem["conversao"] = ok, conv
        if not ok:
            self._erro("SEM003", _inicio(n["valor"]),
                       f'Não é possível atribuir {tv} a {td} sem conversão permitida '
                       f'(destino “{_texto_expr(n["alvo"])}”; expressão “{_texto_expr(n["valor"])}”).',
                       esperado=td, recebido=tv)
        return td

#Vetores
    def _index(self, n):
        base, idx = n["base"], n["indice"]
        sym, elemento = None, ERRO
        if base["node"] == "Id":
            sym = self._resolver(base)
            if sym is not None:
                if sym.categoria == CAT_VETOR:
                    elemento = sym.tipo
                else:
                    self._erro("SEM006", _inicio(base),
                               f'“{sym.nome}” é {_DESC_CATEGORIA[sym.categoria]} '
                               f'e não pode ser indexado(a); apenas vetores.',
                               nome=sym.nome)
        else:
            tb = self._expr(base)
            if tb != ERRO:
                self._erro("SEM006", _inicio(base),
                           f"Expressão do tipo {tb} não pode ser indexada.",
                           recebido=tb)
        ti = self._expr(idx)
        if ti not in (ERRO, INT):
            self._erro("SEM006", _inicio(idx),
                       f'Índice do vetor “{sym.nome if sym is not None else ""}” deve ser int; recebeu {ti} '
                       f'(expressão “{idx.get("valor", "")}”).',
                       esperado=INT, recebido=ti)
        elif (sym is not None and sym.categoria == CAT_VETOR
              and sym.tamanho_const is not None and sym.tamanho_const > 0):
            v = _const_int(idx)
            if v is not None and not (0 <= v < sym.tamanho_const):
                self._erro("SEM006", _inicio(idx),
                           f'Índice {v} fora da faixa do vetor “{sym.nome}” '
                           f'(tamanho {sym.tamanho_const}; índices válidos '
                           f'0..{sym.tamanho_const - 1}).', nome=sym.nome)
        self._sem(n).update(
            tipo_vetor=(f"{elemento}[]" if elemento != ERRO else ERRO),
            tipo_indice=ti, tipo_elemento=elemento)
        return elemento

#Chamadas
    def _arg(self, a):
        if a["node"] == "Id":
            sym = self._resolver(a)
            if sym is None:
                return ERRO
            if sym.categoria == CAT_VETOR:
                return f"{sym.tipo}[]" if sym.tipo != ERRO else ERRO
        return self._expr(a)

    def _call(self, n):
        callee, args = n["callee"], n["argumentos"]
        sym = None
        pos = _inicio(n)
        if callee["node"] == "Id":
            s = self._resolver(callee)
            if s is not None:
                if s.categoria == CAT_FUNCAO:
                    sym = s
                else:
                    self._erro("SEM007", pos,
                               f'"{s.nome}" é {_DESC_CATEGORIA[s.categoria]} e não '
                               f'uma função; não pode ser chamado(a).', nome=s.nome)
        else:
            tc = self._expr(callee)
            if tc != ERRO:
                self._erro("SEM007", pos,
                           f"Expressão do tipo {tc} não pode ser chamada como função.",
                           recebido=tc)
        targs = [self._arg(a) for a in args]
        sem = self._sem(n)
        sem.update(simbolo=sym, tipos_argumentos=targs,
                   assinatura=_assinatura(sym) if sym else None,
                   tipo_retorno=sym.tipo_retorno if sym else ERRO)
        if sym is None:
            return ERRO

        params = sym.parametros
        if len(args) != len(params):
            self._erro("SEM007", pos,
                       f'“{sym.nome}” espera {_plural(len(params), "argumento")}, '
                       f'mas recebeu {len(args)}.', nome=sym.nome,
                       esperado=str(len(params)), recebido=str(len(args)))
        else:
            for i, ((tp, _), ta, a) in enumerate(zip(params, targs, args), 1):
                if tp == ERRO or ta == ERRO:
                    continue
                if not _compativel(tp, ta)[0]:
                    self._erro("SEM008", _inicio(a),
                               f'Argumento {i} de “{sym.nome}”: esperado {tp}, recebido {ta} '
                               f'(expressão “{_texto_expr(a)}”).', nome=sym.nome,
                               esperado=tp, recebido=ta)
        return sym.tipo_retorno

#Integracao
@dataclass
class ResultadoPipeline:
    ast: Optional[dict]
    erros_lexicos: list
    erros_sintaticos: list
    semantico: Optional[ResultadoSemantico]

    @property
    def rejeitado_antes_da_semantica(self):
        return self.semantico is None

    @property
    def sucesso(self):
        return self.semantico is not None and self.semantico.sucesso

def _carregar_parser():
    import importlib.util
    import os
    try:
        import parser as modulo
        if hasattr(modulo, "analisar"):
            return modulo
    except ImportError:
        pass
    caminho = os.path.join(os.path.dirname(os.path.abspath(__file__)), "parser.py")
    spec = importlib.util.spec_from_file_location("parser_projeto", caminho)
    modulo = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(modulo)
    return modulo

def analisar_codigo(codigo: str, anotar=True) -> ResultadoPipeline:
    ast, erros_lex, erros_sint = _carregar_parser().analisar(codigo)
    if erros_lex or erros_sint:
        return ResultadoPipeline(ast, erros_lex, erros_sint, None)
    return ResultadoPipeline(ast, erros_lex, erros_sint,
                             SemanticAnalyzer(ast, anotar=anotar).analyze())

def ast_anotada_para_json(ast, indent=2):
    def padrao(o):
        return o.resumo() if isinstance(o, Simbolo) else str(o)
    return json.dumps(ast, ensure_ascii=False, indent=indent, default=padrao)

def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        print("Uso: python3 analisador_semantico.py <arquivo> [--ast]", file=sys.stderr)
        sys.exit(2)
    with open(args[0], "r", encoding="utf-8") as f:
        codigo = f.read()

    res = analisar_codigo(codigo)
    linhas_saida = []
    if res.rejeitado_antes_da_semantica:
        for e in res.erros_lexicos:
            linhas_saida.append(f'ERRO LÉXICO {e["linha"]}:{e["coluna"]}: {e["erro"]} {e["lexema"]!r}')
        for e in res.erros_sintaticos:
            linhas_saida.append(f'ERRO SINTÁTICO {e["linha"]}:{e["coluna"]}: {e["mensagem"]}')
        linhas_saida.append("Análise semântica não executada (entrada rejeitada pelo scanner/parser).")
        sys.stdout.write("\n".join(linhas_saida))
        sys.exit(2)

    linhas_saida.extend(str(d) for d in res.semantico.diagnosticos)
    if "--ast" in sys.argv:
        linhas_saida.append(ast_anotada_para_json(res.ast))
    n = len(res.semantico.diagnosticos)
    if n == 0:
        linhas_saida.append("Análise semântica concluída: 0 erros; programa aceito.")
    else:
        plural = "erro" if n == 1 else "erros"
        linhas_saida.append(f"Análise semântica concluída: {n} {plural}; programa rejeitado.")
    sys.stdout.write("\n".join(linhas_saida))
    sys.exit(0 if res.sucesso else 1)

if __name__ == "__main__":
    main()