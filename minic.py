#!/usr/bin/env python
import os
import re
import sys
import unicodedata

import scanner as lexer
from analisador_semantico import analisar_codigo
from gerador_ir import GeradorIR
from otimizador_ir import otimizar, otimizar_alvo

OBS_VETOR = ("Observação: o fator de escala do endereço depende da representação "
             "adotada; deve haver cálculo de endereço e operações de leitura/escrita "
             "do vetor.\n")

OBS_SOMBRA = ("Observação: nomes temporários/endereços de símbolos podem substituir "
              "a notação #outer/#inner.\n")

def montar_saida_o2(ir_otimizada):
    #Cabeçalho e IR com todas as otimizações
    cab = ("Resultado: COMPILAR COM SUCESSO\n"
           "Modo de referência: IR otimizada (-O2)\n"
           "IR otimizada (todas as otimizações):\n")
    return cab + ir_otimizada

def montar_saida_o1(r):
    #Relatório do modo -O1
    l1 = "Resultado: COMPILAR COM SUCESSO"
    if r["aviso"]:
        l1 += " (aviso opcional de código inalcançável)"
    linhas = [l1, f"Alvo: {r['alvo']}", "IR de referência (-O0):", r["ref"],
              r["titulo"], r["opt"]] + r["extras"]
    return "\n".join(linhas) + "\n"

def montar_saida(g, ir):
    #Cabeçalho e IR sem otimização
    l1 = "Resultado: COMPILAR COM SUCESSO"
    if g.usou_sombra:
        l1 += " (assumindo a política MINIC de sombreamento léxico)"
    l3 = "IR canônica esperada"
    if g.usou_vetor:
        l3 += " (LOAD/STORE equivalentes aceitos)"
    elif g.usou_logico:
        l3 += " (curto-circuito ou operação lógica explícita)"
    elif g.usou_rotulos:
        l3 += " (rótulos podem variar)"
    cab = f"{l1}\nModo de referência: IR sem otimização (-O0)\n{l3}:\n"
    obs = (OBS_VETOR if g.usou_vetor else "") + (OBS_SOMBRA if g.usou_sombra else "")
    return cab + ir + obs

#Rejeição
def _norm(texto):
    #Normaliza texto para comparação
    t = unicodedata.normalize("NFD", str(texto))
    return t.encode("ascii", "ignore").decode().lower()

def _citados(msg):
    #Trechos entre aspas
    return re.findall(r"[“\"'`‘]([^“”\"'`’‘]+)[”\"'`’]", str(msg))

def _nos(no):
    #Percorre os nós da AST
    if isinstance(no, dict):
        yield no
        for v in no.values():
            if isinstance(v, (dict, list)):
                yield from _nos(v)
    elif isinstance(no, list):
        for v in no:
            yield from _nos(v)

def _tipo_de(expr):
    if not isinstance(expr, dict):
        return None
    if expr.get("node") == "Lit":
        return expr.get("tipo")
    return (expr.get("sem") or {}).get("tipo")

def _nome_da_msg(msg):
    q = _citados(msg)
    if q:
        return q[0]
    m = re.search(r"(?:identificador|variável|variavel|símbolo|simbolo)\s+(\w+)", str(msg), re.I)
    return m.group(1) if m else "?"

def _criterio_semantico(msg, ast):
    n = _norm(msg)
    if "break" in n or "continue" in n:
        cmd = "continue" if "continue" in n else "break"
        return f"{cmd} só pode ocorrer dentro de while/for."
    if "indice" in n or "index" in n:
        return "índice do vetor deve ter tipo int."
    if "retorn" in n or "return" in n:
        for f in _nos(ast):
            if f.get("node") != "Function":
                continue
            for r in _nos(f["corpo"]):
                t = _tipo_de(r.get("valor")) if r.get("node") == "Return" else None
                if t and t != f.get("tipo"):
                    return (f"retorno {t} incompatível com o tipo {f['tipo']} "
                            f"declarado para {f['nome']}.")
    if "argument" in n or "parametr" in n:
        funcs = {f["nome"]: f for f in _nos(ast) if f.get("node") == "Function"}
        for c in _nos(ast):
            if c.get("node") == "Call" and c["callee"].get("nome") in funcs:
                f = funcs[c["callee"]["nome"]]
                if len(c["argumentos"]) != len(f["params"]):
                    return (f"chamada de {f['nome']} requer {len(f['params'])} "
                            f"argumentos e recebeu {len(c['argumentos'])}.")
    if "duplic" in n or "redecl" in n or "ja declar" in n:
        return f"diagnóstico de declaração duplicada de “{_nome_da_msg(msg)}” no mesmo escopo."
    if ("declarad" in n or "definid" in n or "desconhecid" in n) and (
            "nao" in n or "undeclared" in n or "undefined" in n or "desconhecid" in n):
        return (f"diagnóstico de identificador “{_nome_da_msg(msg)}” não declarado; "
                "não emitir IR executável como se a compilação tivesse sucesso.")
    if ("operand" in n or "operador" in n) and "bool" in n:
        ops = [x for x in _citados(msg) if x in {"+", "-", "*", "/", "%"}]
        if not ops:
            ops = [b["op"] for b in _nos(ast) if b.get("node") == "Binary"][:1]
        return (f"operandos de “{ops[0] if ops else '?'}” devem ser numéricos compatíveis; "
                "bool não pode ser usado como int implicitamente.")
    if "bool" in n and "int" in n:
        return "rejeitar conversão implícita bool -> int na versão estrita da especificação."
    return str(msg) #Texto bruto da mensagem

def _criterio_sintatico(e, codigo):
    msg = str(e["mensagem"])
    if ";" in msg or "ponto e virgula" in _norm(msg) or "ponto-e-virgula" in _norm(msg):
        linhas = codigo.splitlines()
        i = e["linha"] - 1
        perto = linhas[max(i - 1, 0):i + 1]
        if any(re.match(r"\s*(int|float|bool|char|string)\b", l) for l in perto):
            return "diagnóstico de “;” esperado após declaração local."
        return "diagnóstico de “;” esperado."
    return msg

def _criterio_lexico(e):
    lex = str(e["lexema"])
    if len(lex) == 1 and not lex.isalnum():
        return f"diagnóstico de símbolo “{lex}” não reconhecido."
    return f"diagnóstico léxico: {e['erro']} “{lex}”."

def montar_rejeicao(categoria, codigo_saida, criterio):
    return (f"Resultado: REJEITAR\n"
            f"Categoria esperada: erro {categoria} (código de saída {codigo_saida})\n"
            f"Critério: {criterio}\n")

def _debug(msg):
    if os.environ.get("MINIC_DEBUG"):
        erro(msg)

EXIT_OK = 0
EXIT_IO = 1
EXIT_LEXICO = 2
EXIT_SINTATICO = 3
EXIT_SEMANTICO = 4

def erro(msg):
    sys.stderr.write(msg + "\n")

def compilar(codigo, nivel=0):
    #Compila e devolve código de saída e texto
    try:
        res = analisar_codigo(codigo)
    except RecursionError:
        erro("Erro: limite de aninhamento excedido")
        return EXIT_IO, None
    except Exception as e: #Inclui limite de aninhamento
        if type(e).__name__ == "LimiteAninhamento":
            erro("Erro: limite de aninhamento excedido")
            return EXIT_IO, None
        raise

    #Léxico
    if res.erros_lexicos:
        for e in res.erros_lexicos:
            _debug(f'ERRO LÉXICO {e["linha"]}:{e["coluna"]}: {e["erro"]} {e["lexema"]!r}')
        crit = _criterio_lexico(res.erros_lexicos[0])
        return EXIT_LEXICO, montar_rejeicao("léxico", EXIT_LEXICO, crit)

    #Sintático
    if res.erros_sintaticos:
        for e in res.erros_sintaticos:
            _debug(f'ERRO SINTÁTICO {e["linha"]}:{e["coluna"]}: {e["mensagem"]}')
        crit = _criterio_sintatico(res.erros_sintaticos[0], codigo)
        return EXIT_SINTATICO, montar_rejeicao("sintático", EXIT_SINTATICO, crit)

    #Semântico
    if not res.sucesso:
        for d in res.semantico.diagnosticos:
            _debug(f"ERRO SEMÂNTICO {d}")
        crit = _criterio_semantico(res.semantico.diagnosticos[0], getattr(res, "ast", None))
        return EXIT_SEMANTICO, montar_rejeicao("semântico", EXIT_SEMANTICO, crit)

    #Geração de IR
    g = GeradorIR()
    ir = g.gerar(res.ast)
    if nivel == 1:
        return EXIT_OK, montar_saida_o1(otimizar_alvo(ir))
    if nivel >= 2:
        return EXIT_OK, montar_saida_o2(otimizar(ir))
    return EXIT_OK, montar_saida(g, ir)

def main():
    nivel = {"1": 1, "O1": 1, "2": 2, "O2": 2}.get(os.environ.get("MINIC_OPT", ""), 0)
    args = []
    for a in sys.argv[1:]:
        if a in ("-O0", "-O1", "-O2"):
            nivel = int(a[2])
        else:
            args.append(a)
    sys.argv = [sys.argv[0]] + args #Argumentos para o scanner
    caminho = lexer.exigir_argumento("minic") #Exige argumento
    #Define o nível por nome de arquivo
    if nivel == 0 and "MINIC_OPT" not in os.environ and re.fullmatch(
            r"O\d+", os.path.splitext(os.path.basename(caminho))[0]):
        nivel = 1
    codigo = lexer.ler_codigo(caminho)
    status, saida = compilar(codigo, nivel)
    if saida is not None: #Imprime a saída
        lexer.escrever_stdout(saida)
    sys.exit(status)

if __name__ == "__main__":
    main()
