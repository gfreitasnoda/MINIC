#!/usr/bin/env python
import json

#Operadores com texto diferente na IR
OPS_IR = {"&&": "and", "||": "or"}

#Recuo base das linhas da saída
BASE = ""

class GeradorIR:
    def __init__(self):
        self.linhas = [] #Linhas formatadas da IR
        self._indent = BASE #Recuo atual
        self._n_temp = 0
        self._rotulos_usados = set()
        self.usou_rotulos = False #Flags de recursos usados
        self.usou_vetor = False
        self.usou_logico = False
        self.usou_sombra = False
        self._laços = [] #Pilha de laços
        self._renome = {} #Nomes na IR para símbolos sombreados
        self._init_feito = set() #For cujo init já foi emitido

    #API
    def gerar(self, ast):
        #Gera a IR a partir da AST
        self.linhas = []
        self.usou_rotulos = self.usou_vetor = self.usou_logico = self.usou_sombra = False
        self.visitar(ast)
        return "\n".join(self.linhas) + "\n"

    #Infra
    def visitar(self, no):
        metodo = getattr(self, "visit_" + no["node"], self.visit_desconhecido)
        return metodo(no)

    def visit_desconhecido(self, no):
        raise NotImplementedError(f"IR: nó sem visitor: {no['node']}")

    def emitir(self, texto):
        self.linhas.append(self._indent + texto)

    def rotulo_def(self, rotulo):
        #Rótulos no recuo base
        self.linhas.append(f"{BASE}{rotulo}:")

    def novo_temp(self):
        self._n_temp += 1
        return f"t{self._n_temp}"

    def novo_rotulo(self, base, fixo=False):
        #Cria rótulo descritivo único na função
        if not fixo:
            self.usou_rotulos = True
        nome, n = base, 1
        while nome in self._rotulos_usados:
            n += 1
            nome = f"{base}_{n}"
        self._rotulos_usados.add(nome)
        return nome

    #Programa e funções
    def visit_Program(self, no):
        for d in no["declaracoes"]:
            self.visitar(d)

    def visit_Function(self, no):
        #Reinicia temporários e rótulos por função
        self._n_temp = 0
        self._rotulos_usados = set()
        self._preparar_nomes(no)
        params = ", ".join(self._nome(p) for p in no["params"])
        self.linhas.append(f"{BASE}func {no['nome']}" + (f"({params})" if params else ""))
        self._indent = BASE + "  "
        self._visitar_itens(no["corpo"]["itens"])
        self._indent = BASE
        self.linhas.append(f"{BASE}endfunc")

    #Nomes e sombreamento
    def _preparar_nomes(self, func):
        #Renomeia símbolos sombreados
        decls = []
        def achar(n):
            if isinstance(n, dict):
                if n.get("node") in ("Param", "VarDecl"):
                    decls.append(n)
                for v in n.values():
                    if isinstance(v, (dict, list)):
                        achar(v)
            elif isinstance(n, list):
                for v in n:
                    achar(v)
        achar(func["params"])
        achar(func["corpo"])
        por_nome = {}
        for d in decls:
            sym = (d.get("sem") or {}).get("simbolo")
            if sym is not None and id(sym) not in {id(x) for x in por_nome.get(d["nome"], [])}:
                por_nome.setdefault(d["nome"], []).append(sym)
        self._renome = {}
        for nome, syms in por_nome.items():
            if len(syms) > 1:
                self.usou_sombra = True
                if len(syms) == 2:
                    sufixos = ["outer", "inner"]
                else:
                    sufixos = [str(i) for i in range(1, len(syms) + 1)]
                for sym, suf in zip(syms, sufixos):
                    self._renome[id(sym)] = f"{nome}#{suf}"

    def _nome(self, no):
        sym = (no.get("sem") or {}).get("simbolo")
        return self._renome.get(id(sym), no["nome"])

    #Comandos
    def visit_Block(self, no):
        self._visitar_itens(no["itens"])

    def _visitar_itens(self, itens):
        #Visita os comandos em ordem
        for k, item in enumerate(itens):
            if item["node"] == "VarDecl":
                self._antecipar_init_for(item, itens[k + 1:])
            elif self._eh_guarda_com_retorno(item, itens[k + 1:]):
                self._gerar_guarda(item, itens[k + 1:])
                return
            self.visitar(item)

    @staticmethod
    def _termina_em_return(no):
        if no["node"] == "Return":
            return True
        if no["node"] == "Block" and no["itens"]:
            return no["itens"][-1]["node"] == "Return"
        return False

    def _eh_guarda_com_retorno(self, item, resto):
        return (item["node"] == "If" and item.get("senao") is None
                and bool(resto) and self._termina_em_return(item["entao"]))

    def _gerar_guarda(self, no, resto):
        c = self.visitar(no["condicao"])
        l_rec, l_fim = self.novo_rotulo("L_rec", fixo=True), self.novo_rotulo("L_end", fixo=True)
        self.emitir(f"ifFalse {c} goto {l_rec}")
        self.visitar(no["entao"])
        self.emitir(f"goto {l_fim}")
        self.rotulo_def(l_rec)
        self._visitar_itens(resto)
        self.rotulo_def(l_fim)

    @staticmethod
    def _usa_nome(no, nome):
        if isinstance(no, dict):
            if no.get("nome") == nome:
                return True
            return any(GeradorIR._usa_nome(v, nome) for v in no.values()
                       if isinstance(v, (dict, list)))
        if isinstance(no, list):
            return any(GeradorIR._usa_nome(v, nome) for v in no)
        return False

    def _antecipar_init_for(self, decl, resto):
        if decl.get("valor") is not None or decl.get("tamanho") is not None:
            return
        for j, item in enumerate(resto):
            if item["node"] == "For":
                init = item.get("init")
                if init is not None and init["node"] == "ExprStmt":
                    init = init.get("expressao")
                if (init is not None and init["node"] == "Assign"
                        and init["alvo"]["node"] == "Id"
                        and init["alvo"]["nome"] == decl["nome"]
                        and init["valor"]["node"] == "Lit"):
                    self.visitar(init)
                    self._init_feito.add(id(item))
                return
            if self._usa_nome(item, decl["nome"]):
                return

    def visit_VarDecl(self, no):
        if no.get("tamanho") is not None:
            return #Vetor não gera instrução
        if no.get("valor") is not None:
            v = self.visitar(no["valor"])
            self.emitir(f"{self._nome(no)} = {v}")
        #Sem inicializador não gera instrução

    def visit_ExprStmt(self, no):
        if no.get("expressao") is not None:
            self._expr_como_comando(no["expressao"])

    def _expr_como_comando(self, expr):
        #Chamada void sem temporário
        if expr["node"] == "Call" and self._retorno_void(expr):
            self._gerar_chamada(expr, com_resultado=False)
        else:
            self.visitar(expr)

    def visit_Print(self, no):
        self.emitir(f"print {self.visitar(no['valor'])}")

    def visit_Read(self, no):
        alvo = no["alvo"]
        if alvo["node"] == "Index":
            base, ender = self._endereco(alvo)
            self.emitir(f"read {base}[{ender}]")
        else:
            self.emitir(f"read {self._nome(alvo)}")

    def visit_Return(self, no):
        if no.get("valor") is None:
            self.emitir("return")
        else:
            self.emitir(f"return {self.visitar(no['valor'])}")

    def visit_If(self, no):
        c = self.visitar(no["condicao"])
        tem_else = no.get("senao") is not None
        l_else = self.novo_rotulo("L_else" if tem_else else "L_end_if")
        self.emitir(f"ifFalse {c} goto {l_else}")
        self.visitar(no["entao"])
        if tem_else:
            l_fim = self.novo_rotulo("L_end")
            self.emitir(f"goto {l_fim}")
            self.rotulo_def(l_else)
            self.visitar(no["senao"])
            self.rotulo_def(l_fim)
        else:
            self.rotulo_def(l_else)

    def visit_While(self, no):
        l_ini, l_fim = self.novo_rotulo("L_test"), self.novo_rotulo("L_end")
        self.rotulo_def(l_ini)
        c = self.visitar(no["condicao"])
        self.emitir(f"ifFalse {c} goto {l_fim}")
        self._laços.append([l_ini, l_fim, False])
        self.visitar(no["corpo"])
        self._laços.pop()
        self.emitir(f"goto {l_ini}")
        self.rotulo_def(l_fim)

    def visit_For(self, no):
        l_cond, l_inc, l_fim = self.novo_rotulo("L_test"), self.novo_rotulo("L_inc"), self.novo_rotulo("L_end")
        if no.get("init") is not None and id(no) not in self._init_feito:
            self.visitar(no["init"])
        self.rotulo_def(l_cond)
        if no.get("condicao") is not None:
            c = self.visitar(no["condicao"])
            self.emitir(f"ifFalse {c} goto {l_fim}")
        self._laços.append([l_inc, l_fim, False]) #Continue vai ao incremento
        self.visitar(no["corpo"])
        usou_continue = self._laços.pop()[2]
        if usou_continue:
            self.rotulo_def(l_inc)
        if no.get("incremento") is not None:
            self._expr_como_comando(no["incremento"])
        self.emitir(f"goto {l_cond}")
        self.rotulo_def(l_fim)

    def visit_Break(self, no):
        self.emitir(f"goto {self._laços[-1][1]}")

    def visit_Continue(self, no):
        self._laços[-1][2] = True
        self.emitir(f"goto {self._laços[-1][0]}")

    #Expressões
    def visit_Lit(self, no):
        t, v = no["tipo"], no["valor"]
        if t == "bool":
            return "true" if v else "false"
        if t == "string":
            return json.dumps(v, ensure_ascii=False)
        if t == "char":
            return "'" + str(v).replace("'", "\\'") + "'"
        return str(v) #Int ou float

    def visit_Id(self, no):
        return self._nome(no)

    def visit_Binary(self, no):
        e = self.visitar(no["esquerda"]) #Esquerda antes da direita
        d = self.visitar(no["direita"])
        t = self.novo_temp()
        op = OPS_IR.get(no["op"], no["op"])
        self.usou_logico |= op in ("and", "or")
        self.emitir(f"{t} = {e} {op} {d}")
        return t

    def visit_Unary(self, no):
        o = self.visitar(no["operando"])
        t = self.novo_temp()
        if no["op"] == "!":
            self.usou_logico = True
            self.emitir(f"{t} = not {o}")
        else:
            self.emitir(f"{t} = {no['op']}{o}")
        return t

    def visit_Index(self, no):
        base, ender = self._endereco(no)
        t = self.novo_temp()
        self.emitir(f"{t} = load {base}[{ender}]")
        return t

    ESCALA = 1 #Tamanho do elemento

    def _endereco(self, idx_no):
        #Calcula o deslocamento do índice
        self.usou_vetor = True
        base = self._nome(idx_no["base"])
        idx = self.visitar(idx_no["indice"])
        t = self.novo_temp()
        self.emitir(f"{t} = {idx} * {self.ESCALA}")
        return base, t

    def visit_Assign(self, no):
        alvo = no["alvo"]
        if alvo["node"] == "Index": #Índice antes do valor
            base, ender = self._endereco(alvo)
            v = self.visitar(no["valor"])
            self.emitir(f"store {base}[{ender}], {v}")
            return v
        v = self.visitar(no["valor"])
        destino = self._nome(alvo)
        self.emitir(f"{destino} = {v}")
        return destino #Atribuição encadeada

    def visit_Call(self, no):
        return self._gerar_chamada(no, com_resultado=True)

    def _retorno_void(self, no):
        return (no.get("sem") or {}).get("tipo_retorno") == "void"

    def _gerar_chamada(self, no, com_resultado):
        #Avalia argumentos, emite param e call
        ops = [self.visitar(a) for a in no["argumentos"]]
        for op in ops:
            self.emitir(f"param {op}")
        nome = no["callee"]["nome"]
        chamada = f"call {nome}, {len(ops)}"
        if not com_resultado:
            self.emitir(chamada)
            return None
        t = self.novo_temp() #Temporário após os argumentos
        self.emitir(f"{t} = {chamada}")
        return t
