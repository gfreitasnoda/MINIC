#!/usr/bin/env python
import re
from collections import Counter

RE_INT = re.compile(r"^-?\d+$")
RE_NUM = re.compile(r"^-?(\d+\.\d*|\.\d+|\d+)([eE][+-]?\d+)?$")
RE_TEMP = re.compile(r"^t\d+$")
INT_MIN, INT_MAX = -2 ** 31, 2 ** 31 - 1
COMUTATIVOS = {"+", "*", "==", "!=", "and", "or"}

class Cfg:
    #Opções de transformação da propagação
    def __init__(self, const=True, alg=True, copia="all"):
        self.const, self.alg, self.copia = const, alg, copia

CFG_TOTAL = Cfg()

def eh_lit(x):
    return bool(RE_NUM.match(x)) or x in ("true", "false") or x[0] in "\"'"

#Instruções como tuplas

def _parse_rhs(dst, rhs):
    if rhs[0] in "\"'":
        return ("copy", dst, rhs)
    p = rhs.split(" ")
    if len(p) == 1:
        if eh_lit(rhs):
            return ("copy", dst, rhs)
        if rhs[0] == "-":
            return ("un", dst, "-", rhs[1:])
        return ("copy", dst, rhs)
    if len(p) == 2 and p[0] == "not":
        return ("un", dst, "not", p[1])
    if len(p) == 3:
        return ("bin", dst, p[0], p[1], p[2])
    raise ValueError(rhs)

def parse(txt):
    t = txt.strip()
    m = re.match(r"^(\S+):$", t)
    if m:
        return ("label", m.group(1))
    m = re.match(r"^ifFalse (\S+) goto (\S+)$", t)
    if m:
        return ("ifFalse", m.group(1), m.group(2))
    m = re.match(r"^goto (\S+)$", t)
    if m:
        return ("goto", m.group(1))
    if t == "return":
        return ("ret", None)
    m = re.match(r"^return (.+)$", t)
    if m:
        return ("ret", m.group(1))
    m = re.match(r"^print (.+)$", t)
    if m:
        return ("print", m.group(1))
    m = re.match(r"^param (.+)$", t)
    if m:
        return ("param", m.group(1))
    m = re.match(r"^read (\S+?)\[(\S+)\]$", t)
    if m:
        return ("readidx", m.group(1), m.group(2))
    m = re.match(r"^read (\S+)$", t)
    if m:
        return ("read", m.group(1))
    m = re.match(r"^store (\S+?)\[(\S+)\], (.+)$", t)
    if m:
        return ("store", m.group(1), m.group(2), m.group(3))
    m = re.match(r"^call (\S+), (\d+)$", t)
    if m:
        return ("call", None, m.group(1), m.group(2))
    m = re.match(r"^(\S+) = (.+)$", t)
    if m:
        d, rhs = m.group(1), m.group(2)
        m2 = re.match(r"^load (\S+?)\[(\S+)\]$", rhs)
        if m2:
            return ("load", d, m2.group(1), m2.group(2))
        m2 = re.match(r"^call (\S+), (\d+)$", rhs)
        if m2:
            return ("call", d, m2.group(1), m2.group(2))
        return _parse_rhs(d, rhs)
    raise ValueError(txt)

def fmt(i):
    k = i[0]
    if k == "label":
        return f"{i[1]}:"
    if k == "copy":
        return f"  {i[1]} = {i[2]}"
    if k == "bin":
        return f"  {i[1]} = {i[2]} {i[3]} {i[4]}"
    if k == "un":
        return f"  {i[1]} = " + (f"-{i[3]}" if i[2] == "-" else f"not {i[3]}")
    if k == "load":
        return f"  {i[1]} = load {i[2]}[{i[3]}]"
    if k == "store":
        return f"  store {i[1]}[{i[2]}], {i[3]}"
    if k == "ifFalse":
        return f"  ifFalse {i[1]} goto {i[2]}"
    if k == "goto":
        return f"  goto {i[1]}"
    if k == "print":
        return f"  print {i[1]}"
    if k == "read":
        return f"  read {i[1]}"
    if k == "readidx":
        return f"  read {i[1]}[{i[2]}]"
    if k == "ret":
        return "  return" if i[1] is None else f"  return {i[1]}"
    if k == "param":
        return f"  param {i[1]}"
    if k == "call":
        c = f"call {i[2]}, {i[3]}"
        return f"  {i[1]} = {c}" if i[1] else f"  {c}"
    raise ValueError(i)

def usos(i):
    k = i[0]
    if k == "copy":
        ops = [i[2]]
    elif k == "bin":
        ops = [i[2], i[4]]
    elif k == "un":
        ops = [i[3]]
    elif k == "load":
        ops = [i[3]]
    elif k == "store":
        ops = [i[2], i[3]]
    elif k == "ifFalse":
        ops = [i[1]]
    elif k in ("print", "param"):
        ops = [i[1]]
    elif k == "ret":
        ops = [] if i[1] is None else [i[1]]
    elif k == "readidx":
        ops = [i[2]]
    else:
        ops = []
    return [o for o in ops if not eh_lit(o)]

def defs(i):
    return i[1] if i[0] in ("copy", "bin", "un", "load", "read", "call") else None

#CFG
def construir_cfg(instrs):
    blocos, atual = [], []
    for ins in instrs:
        if atual and (ins[0] == "label" or atual[-1][0] in ("goto", "ifFalse", "ret")):
            blocos.append(atual)
            atual = []
        atual.append(ins)
    if atual:
        blocos.append(atual)
    rot = {b[0][1]: n for n, b in enumerate(blocos) if b[0][0] == "label"}
    sucs = []
    for n, b in enumerate(blocos):
        u = b[-1]
        prox = [n + 1] if n + 1 < len(blocos) else []
        if u[0] == "goto":
            sucs.append([rot[u[1]]])
        elif u[0] == "ifFalse":
            sucs.append([rot[u[2]]] + prox)
        elif u[0] == "ret":
            sucs.append([])
        else:
            sucs.append(prox)
    return blocos, sucs

#Propagação
def _subst(op, st):
    return op if eh_lit(op) else st.get(op, op)

def _kill(st, nome):
    st.pop(nome, None)
    for k in [k for k, v in st.items() if v == nome]:
        del st[k]

def _def(st, d, val=None):
    _kill(st, d)
    if val is not None and val != d:
        st[d] = val

def _cabe(v):
    return INT_MIN <= v <= INT_MAX

def _div(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q

def fold_bin(op, a, b):
    #Dobra de constantes
    if RE_INT.match(a) and RE_INT.match(b):
        x, y = int(a), int(b)
        if op in ("+", "-", "*"):
            r = x + y if op == "+" else x - y if op == "-" else x * y
            return str(r) if _cabe(r) else None
        if op == "/" and y != 0:
            return str(_div(x, y))
        if op == "%" and y != 0:
            return str(x - y * _div(x, y))
        cmp_ = {"<": x < y, "<=": x <= y, ">": x > y, ">=": x >= y,
                "==": x == y, "!=": x != y}
        if op in cmp_:
            return "true" if cmp_[op] else "false"
        return None
    if a in ("true", "false") and b in ("true", "false"):
        x, y = a == "true", b == "true"
        r = {"==": x == y, "!=": x != y, "and": x and y, "or": x or y}.get(op)
        return None if r is None else ("true" if r else "false")
    return None

def alg_bin(op, a, b):
    #Identidades algébricas
    if op == "+":
        if b == "0":
            return a
        if a == "0":
            return b
    elif op == "-":
        if b == "0":
            return a
    elif op == "*":
        if b == "1":
            return a
        if a == "1":
            return b
    elif op == "/":
        if b == "1":
            return a
    elif op == "and":
        if a == "false" or b == "false":
            return "false"
        if a == "true":
            return b
        if b == "true":
            return a
    elif op == "or":
        if a == "true" or b == "true":
            return "true"
        if a == "false":
            return b
        if b == "false":
            return a
    return None

def simp_bin(op, a, b, cfg=CFG_TOTAL):
    r = fold_bin(op, a, b) if cfg.const else None
    if r is None and cfg.alg:
        r = alg_bin(op, a, b)
    return r

def simp_un(op, a):
    if op == "-" and RE_INT.match(a):
        r = -int(a)
        return str(r) if _cabe(r) else None
    if op == "not" and a in ("true", "false"):
        return "false" if a == "true" else "true"
    return None

def _guardar(cfg, d, v):
    #Pode lembrar o valor de d
    if v is None:
        return None
    if eh_lit(v):
        return v if cfg.const else None
    if cfg.copia == "all" or (cfg.copia == "temps" and RE_TEMP.match(d)):
        return v
    return None

def passo(i, st, glob, cfg=CFG_TOTAL):
    #Transforma uma instrução e atualiza o estado
    k = i[0]
    if k == "copy":
        s = _subst(i[2], st)
        if s == i[1]:
            return None
        _def(st, i[1], _guardar(cfg, i[1], s))
        return ("copy", i[1], s)
    if k == "bin":
        _, d, a, op, b = i
        a, b = _subst(a, st), _subst(b, st)
        r = simp_bin(op, a, b, cfg)
        if r is not None:
            if r == d:
                return None
            _def(st, d, _guardar(cfg, d, r))
            return ("copy", d, r)
        _def(st, d)
        return ("bin", d, a, op, b)
    if k == "un":
        _, d, op, a = i
        a = _subst(a, st)
        r = simp_un(op, a) if cfg.const else None
        if r is not None:
            _def(st, d, _guardar(cfg, d, r))
            return ("copy", d, r)
        _def(st, d)
        return ("un", d, op, a)
    if k == "load":
        idx = _subst(i[3], st)
        _def(st, i[1])
        return ("load", i[1], i[2], idx)
    if k == "store":
        return ("store", i[1], _subst(i[2], st), _subst(i[3], st))
    if k == "ifFalse":
        c = _subst(i[1], st)
        if cfg.const and c == "true":
            return None
        if cfg.const and c == "false":
            return ("goto", i[2])
        return ("ifFalse", c, i[2])
    if k in ("print", "param"):
        return (k, _subst(i[1], st))
    if k == "ret":
        return ("ret", None if i[1] is None else _subst(i[1], st))
    if k == "read":
        _def(st, i[1])
        return i
    if k == "readidx":
        return ("readidx", i[1], _subst(i[2], st))
    if k == "call":
        if i[1]:
            _def(st, i[1])
        for g in [g for g, v in st.items() if g in glob or v in glob]:
            del st[g]
        return i
    return i

def propagar(instrs, glob, cfg=CFG_TOTAL):
    blocos, sucs = construir_cfg(instrs)
    if not blocos:
        return instrs
    n = len(blocos)
    preds = [[] for _ in range(n)]
    for b, ss in enumerate(sucs):
        for s in ss:
            preds[s].append(b)
    ent, sai = [None] * n, [None] * n
    for _ in range(200):
        mudou = False
        for b in range(n):
            if b == 0:
                e = {}
            else:
                ps = [sai[p] for p in preds[b] if sai[p] is not None]
                if not ps:
                    continue
                e = dict(ps[0])
                for o in ps[1:]:
                    e = {k: v for k, v in e.items() if o.get(k) == v}
            st = dict(e)
            for ins in blocos[b]:
                passo(ins, st, glob, cfg)
            if e != ent[b] or st != sai[b]:
                ent[b], sai[b] = e, st
                mudou = True
        if not mudou:
            break
    else:
        return instrs #Sem convergência: não arrisca
    novos = []
    for b in range(n):
        if ent[b] is None:
            novos.extend(blocos[b])
            continue
        st = dict(ent[b])
        for ins in blocos[b]:
            r = passo(ins, st, glob, cfg)
            if r is not None:
                novos.append(r)
    return novos

#CSE local
def cse_local(instrs, glob):
    av, out = {}, []

    def kill(nome):
        for k in [k for k, h in av.items() if h == nome or nome in k[2:]]:
            del av[k]

    for ins in instrs:
        k = ins[0]
        if k == "label":
            av.clear()
            out.append(ins)
            continue
        if k in ("bin", "un"):
            if k == "bin":
                _, d, a, op, b = ins
                key = None
                if not (eh_lit(a) and eh_lit(b)):
                    x, y = (b, a) if op in COMUTATIVOS and a > b else (a, b)
                    key = ("bin", op, x, y)
            else:
                _, d, op, a = ins
                key = ("un", op, a)
            if key is not None and key in av and av[key] != d:
                kill(d)
                out.append(("copy", d, av[key]))
                continue
            kill(d)
            if key is not None and d not in key[2:]:
                av[key] = d
            out.append(ins)
            continue
        d = defs(ins)
        if d is not None:
            kill(d)
        if k == "call":
            for kk in [kk for kk, h in av.items()
                       if h in glob or any(o in glob for o in kk[2:])]:
                del av[kk]
        out.append(ins)
    return out

#Fluxo de controle
def limpar_fluxo(instrs):
    blocos, sucs = construir_cfg(instrs)
    if not blocos:
        return instrs
    alc, pilha = {0}, [0]
    while pilha:
        b = pilha.pop()
        for s in sucs[b]:
            if s not in alc:
                alc.add(s)
                pilha.append(s)
    out = [ins for n, b in enumerate(blocos) if n in alc for ins in b]
    while True:
        alvos = ({i[2] for i in out if i[0] == "ifFalse"}
                 | {i[1] for i in out if i[0] == "goto"})
        novo, mudou = [], False
        for idx, ins in enumerate(out):
            if ins[0] == "label" and ins[1] not in alvos:
                mudou = True
                continue
            if ins[0] in ("goto", "ifFalse"):
                alvo = ins[1] if ins[0] == "goto" else ins[2]
                j, prox = idx + 1, False
                while j < len(out) and out[j][0] == "label":
                    if out[j][1] == alvo:
                        prox = True
                        break
                    j += 1
                if prox:
                    mudou = True
                    continue
            novo.append(ins)
        out = novo
        if not mudou:
            return out

#Code motion
def _div_perigosa(ins):
    return ins[0] == "bin" and ins[3] in ("/", "%") and not (
        RE_INT.match(ins[4]) and int(ins[4]) != 0)

def _entrada_unica(instrs, rot, j, i):
    #Sem salto externo para dentro do laço
    for p, ins in enumerate(instrs):
        alvo = (ins[1] if ins[0] == "goto"
                else ins[2] if ins[0] == "ifFalse" else None)
        if alvo in rot and j <= rot[alvo] <= i and not (j <= p <= i):
            return False
    return True

def mover_invariantes(instrs, glob, registro=None):
    rot = {ins[1]: i for i, ins in enumerate(instrs) if ins[0] == "label"}
    defs_total = Counter(d for d in map(defs, instrs) if d is not None)
    lacos = sorted((i - rot[ins[1]], rot[ins[1]], i)
                   for i, ins in enumerate(instrs)
                   if ins[0] == "goto" and ins[1] in rot and rot[ins[1]] < i)
    for _, j, i in lacos:
        if not _entrada_unica(instrs, rot, j, i):
            continue
        corpo = instrs[j + 1:i]
        tem_call = any(x[0] == "call" for x in corpo)
        defs_laco = Counter(d for d in map(defs, corpo) if d is not None)
        mover, invar, mudou = set(), set(), True
        while mudou:
            mudou = False
            for p in range(j + 1, i):
                ins = instrs[p]
                if p in mover or ins[0] not in ("bin", "un"):
                    continue
                d = ins[1]
                if (not RE_TEMP.match(d) or defs_laco[d] != 1 or defs_total[d] != 1
                        or _div_perigosa(ins)):
                    continue
                if all(o in invar or (defs_laco[o] == 0 and not (tem_call and o in glob))
                       for o in usos(ins)):
                    mover.add(p)
                    invar.add(d)
                    mudou = True
        if mover:
            if registro is not None:
                registro.extend(instrs[p] for p in sorted(mover))
            return (instrs[:j] + [instrs[p] for p in sorted(mover)]
                    + [x for p, x in enumerate(instrs[j:], start=j) if p not in mover])
    return instrs

#Vivacidade e código morto
def _removivel(ins):
    k = ins[0]
    if k in ("copy", "un", "load"):
        return True
    if k == "bin":
        return not _div_perigosa(ins)
    return False

def _vivas_entrada(blocos, sucs, glob):
    n = len(blocos)
    vin = [set() for _ in range(n)]
    for _ in range(300):
        mudou = False
        for b in range(n - 1, -1, -1):
            live = set()
            for s in sucs[b]:
                live |= vin[s]
            for ins in reversed(blocos[b]):
                d = defs(ins)
                if d is not None and d not in glob:
                    live.discard(d)
                live.update(usos(ins))
            if live != vin[b]:
                vin[b] = live
                mudou = True
        if not mudou:
            return vin
    raise RuntimeError("vivacidade não convergiu")

def eliminar_mortos(instrs, glob):
    blocos, sucs = construir_cfg(instrs)
    if not blocos:
        return instrs
    vin = _vivas_entrada(blocos, sucs, glob)
    out = []
    for b, bl in enumerate(blocos):
        live = set()
        for s in sucs[b]:
            live |= vin[s]
        rev = []
        for ins in reversed(bl):
            d = defs(ins)
            if d is not None and d not in live and d not in glob:
                if _removivel(ins):
                    continue
                if ins[0] == "call":
                    ins = ("call", None, ins[2], ins[3])
            d = defs(ins)
            if d is not None and d not in glob:
                live.discard(d)
            live.update(usos(ins))
            rev.append(ins)
        out.extend(reversed(rev))
    return out

def fundir_temporarios(instrs, glob):
    blocos, sucs = construir_cfg(instrs)
    if not blocos:
        return instrs
    vin = _vivas_entrada(blocos, sucs, glob)
    out = []
    for b, bl in enumerate(blocos):
        live = set()
        for s in sucs[b]:
            live |= vin[s]
        n = len(bl)
        apos = [None] * n
        for p in range(n - 1, -1, -1):
            apos[p] = set(live)
            d = defs(bl[p])
            if d is not None and d not in glob:
                live.discard(d)
            live.update(usos(bl[p]))
        p = 0
        while p < n:
            ins = bl[p]
            if (ins[0] in ("bin", "un", "load", "call") and ins[1] is not None
                    and RE_TEMP.match(ins[1]) and p + 1 < n
                    and bl[p + 1][0] == "copy" and bl[p + 1][2] == ins[1]
                    and ins[1] not in apos[p + 1]):
                out.append((ins[0], bl[p + 1][1]) + ins[2:])
                p += 2
                continue
            out.append(ins)
            p += 1
    return out

#Redução de força
def reduzir_forca(instrs, glob):
    #Redução de força de variável de indução
    rot = {ins[1]: i for i, ins in enumerate(instrs) if ins[0] == "label"}
    defs_total = Counter(d for d in map(defs, instrs) if d is not None)
    usos_total = Counter(o for ins in instrs for o in usos(ins))
    lacos = sorted((i - rot[ins[1]], rot[ins[1]], i)
                   for i, ins in enumerate(instrs)
                   if ins[0] == "goto" and ins[1] in rot and rot[ins[1]] < i)

    def fora(alvo, j, i):
        return alvo in rot and not (j <= rot[alvo] <= i)

    for _, j, i in lacos:
        if not _entrada_unica(instrs, rot, j, i):
            continue
        for q in range(j + 1, i - 1):
            a, b = instrs[q], instrs[q + 1]
            if not (a[0] == "bin" and a[3] in ("+", "-") and RE_INT.match(a[4])
                    and RE_TEMP.match(a[1]) and a[2] == b[1] and b[0] == "copy"
                    and b[2] == a[1] and not RE_TEMP.match(b[1])):
                continue
            v, t, op, c = b[1], a[1], a[3], a[4]
            if defs_total[t] != 1 or usos_total[t] != 1:
                continue
            if sum(1 for x in instrs[j + 1:i] if defs(x) == v) != 1:
                continue
            #Saltos só para fora do laço
            ok = True
            for x in instrs[j + 1:q]:
                if x[0] == "label":
                    ok = False
                if x[0] == "goto" and not fora(x[1], j, i):
                    ok = False
                if x[0] == "ifFalse" and not fora(x[2], j, i):
                    ok = False
            if not ok:
                continue
            #Valor inicial literal antes do laço
            k0 = None
            for k in range(j - 1, -1, -1):
                x = instrs[k]
                if x[0] in ("label", "goto", "ifFalse", "ret"):
                    break
                if defs(x) == v:
                    if x[0] == "copy" and RE_INT.match(x[2]):
                        k0 = int(x[2])
                    break
            if k0 is None:
                continue
            passo_v = int(c) if op == "+" else -int(c)
            for p in range(j + 1, q - 1):
                x, y = instrs[p], instrs[p + 1]
                if not (x[0] == "bin" and x[3] == "*" and RE_TEMP.match(x[1])
                        and y[0] == "copy" and y[2] == x[1]):
                    continue
                if x[2] == v and RE_INT.match(x[4]):
                    m = int(x[4])
                elif x[4] == v and RE_INT.match(x[2]):
                    m = int(x[2])
                else:
                    continue
                w = y[1]
                if (RE_TEMP.match(w) or w == v or defs_total[x[1]] != 1
                        or usos_total[x[1]] != 1 or defs_total[w] != 1):
                    continue
                pos_uso = [n for n, z in enumerate(instrs) if w in usos(z)]
                if any(not (p + 2 <= n <= q - 1) for n in pos_uso):
                    continue
                ini_w, inc_w = k0 * m, passo_v * m
                if not (_cabe(ini_w) and _cabe(inc_w)):
                    continue
                atual_w = (("bin", w, w, "+", str(inc_w)) if inc_w >= 0
                           else ("bin", w, w, "-", str(-inc_w)))
                return (instrs[:j] + [("copy", w, str(ini_w))] + instrs[j:p]
                        + instrs[p + 2:q] + [("bin", v, v, op, c), atual_w]
                        + instrs[q + 2:])
    return instrs

#Numeração dos temporários
def compactar_temps(instrs):
    #Renumera temporários sem buracos
    nums = sorted({int(x[1:]) for ins in instrs for n, x in enumerate(ins)
                   if isinstance(x, str) and n > 0 and RE_TEMP.match(x)
                   and not (ins[0] == "call" and n == 2)})
    mapa = {f"t{n}": f"t{r}" for r, n in enumerate(nums, start=1)}
    if all(k == v for k, v in mapa.items()):
        return instrs
    return [tuple(mapa.get(x, x) if isinstance(x, str) and n > 0
                  and not (ins[0] == "call" and n == 2) else x
                  for n, x in enumerate(ins)) for ins in instrs]

#Alvos
def _estavel(instrs, glob, passes, limite=30):
    for _ in range(limite):
        antes = list(instrs)
        for f in passes:
            instrs = f(instrs, glob)
        if instrs == antes:
            break
    return instrs

CFG_CONST = Cfg(const=True, alg=True, copia=None)
CFG_ALG = Cfg(const=False, alg=True, copia="all")
CFG_COPIA = Cfg(const=False, alg=False, copia="all")
CFG_CSE = Cfg(const=False, alg=False, copia="temps")

def _tem_inalcancavel(instrs):
    morto = False
    for ins in instrs:
        if ins[0] == "label":
            morto = False
        elif morto:
            return True
        if ins[0] in ("ret", "goto"):
            morto = True
    return False

def _tem_algebra(instrs):
    return any(i[0] == "bin" and not (eh_lit(i[2]) and eh_lit(i[4]))
               and alg_bin(i[3], i[2], i[4]) is not None for i in instrs)

def _tem_copia_de_usuario(instrs):
    return any(i[0] == "copy" and not eh_lit(i[2]) and not RE_TEMP.match(i[2])
               and not RE_TEMP.match(i[1]) for i in instrs)

def _loop_licm(instrs, glob, registro=None):
    for _ in range(30):
        novo = mover_invariantes(instrs, glob, registro)
        if novo == instrs:
            break
        instrs = novo
    return instrs

def _loop_sr(instrs, glob):
    for _ in range(30):
        novo = reduzir_forca(instrs, glob)
        if novo == instrs:
            break
        instrs = novo
    return instrs

def _prop(cfg):
    return lambda ins, g: propagar(ins, g, cfg)

#Nome, detecção e aplicação em ordem de prioridade
ALVOS = [
    ("inalcancavel", lambda f, g: _tem_inalcancavel(f),
     lambda f, g: compactar_temps(limpar_fluxo(f))),
    ("licm", lambda f, g: _loop_licm(f, g) != f,
     lambda f, g: compactar_temps(_loop_licm(f, g))),
    ("forca", lambda f, g: reduzir_forca(f, g) != f,
     lambda f, g: compactar_temps(eliminar_mortos(_loop_sr(f, g), g))),
    ("cse", lambda f, g: cse_local(f, g) != f,
     lambda f, g: compactar_temps(_estavel(
         f, g, [cse_local, _prop(CFG_CSE), eliminar_mortos]))),
    ("algebra", lambda f, g: _tem_algebra(f),
     lambda f, g: compactar_temps(_estavel(
         f, g, [_prop(CFG_ALG), eliminar_mortos]))),
    ("copias", lambda f, g: _tem_copia_de_usuario(f),
     lambda f, g: compactar_temps(_estavel(
         f, g, [_prop(CFG_COPIA), eliminar_mortos]))),
    ("morta", lambda f, g: eliminar_mortos(f, g) != f,
     lambda f, g: compactar_temps(eliminar_mortos(f, g))),
    ("constantes", lambda f, g: propagar(f, g, CFG_CONST) != f,
     lambda f, g: compactar_temps(_estavel(
         f, g, [_prop(CFG_CONST), lambda i, _g: limpar_fluxo(i), eliminar_mortos]))),
    ("geral", lambda f, g: otimizar_funcao(f, g) != f,
     lambda f, g: compactar_temps(otimizar_funcao(f, g))),
]

#Driver
def otimizar_funcao(instrs, glob):
    for _ in range(30):
        antes = list(instrs)
        instrs = propagar(instrs, glob)
        instrs = cse_local(instrs, glob)
        instrs = limpar_fluxo(instrs)
        instrs = mover_invariantes(instrs, glob)
        instrs = eliminar_mortos(instrs, glob)
        if instrs == antes:
            break
    instrs = fundir_temporarios(instrs, glob)
    return limpar_fluxo(instrs)

def _segmentar(ir_texto):
    #Separa partes e acha globais
    linhas = ir_texto.rstrip("\n").split("\n")
    partes, i = [], 0
    while i < len(linhas):
        if linhas[i].startswith("func "):
            j = i + 1
            while linhas[j] != "endfunc":
                j += 1
            m = re.match(r"^func (\S+?)(?:\((.*)\))?$", linhas[i])
            params = [p.strip() for p in (m.group(2) or "").split(",") if p.strip()]
            try:
                corpo = [parse(l) for l in linhas[i + 1:j]]
            except ValueError:
                corpo = None #Linha desconhecida
            partes.append(["func", linhas[i], params, corpo, linhas[i + 1:j], m.group(1)])
            i = j + 1
        else:
            partes.append(["raw", linhas[i]])
            i += 1
    glob = set()
    for p in partes:
        if p[0] == "raw":
            m = re.match(r"^\s*(\S+) = ", p[1])
            if m:
                glob.add(m.group(1))
    por_func = []
    for p in partes:
        if p[0] == "func" and p[3] is not None:
            nomes = set()
            for ins in p[3]:
                d = defs(ins)
                nomes.update(([d] if d else []) + usos(ins))
            por_func.append({x for x in nomes if not RE_TEMP.match(x)} - set(p[2]))
    cont = Counter(x for s in por_func for x in s)
    glob |= {x for x, c in cont.items() if c > 1}
    return partes, glob

def _montar(partes, corpos):
    #Monta o texto do programa
    saida, k = [], 0
    for p in partes:
        if p[0] == "raw":
            saida.append(p[1])
            continue
        saida.append(p[1])
        saida.extend(corpos[k] if isinstance(corpos[k], list) else [])
        saida.append("endfunc")
        k += 1
    return "\n".join(saida) + "\n"

def _aplicar(partes, glob, aplica):
    corpos = []
    for p in partes:
        if p[0] != "func":
            continue
        if p[3] is None:
            corpos.append(p[4])
            continue
        try:
            corpos.append([fmt(x) for x in aplica(p[3], glob)])
        except (KeyError, RuntimeError):
            corpos.append(p[4])
    return corpos

def otimizar(ir_texto):
    #Aplica todas as otimizações
    partes, glob = _segmentar(ir_texto)
    return _montar(partes, _aplicar(partes, glob, otimizar_funcao))

#Relatório por alvo
def _juntar(nomes):
    nomes = list(nomes)
    return nomes[0] if len(nomes) == 1 else ", ".join(nomes[:-1]) + " e " + nomes[-1]

def _corpo_exibicao(partes, corpos, referencia):
    #Escolhe o trecho a exibir
    funcs = [p for p in partes if p[0] == "func"]
    if len(funcs) == 1 and funcs[0][5] == "main" and len(partes) == 1:
        linhas = list(corpos[0])
        if referencia and linhas and linhas[-1] == "  return 0":
            linhas.pop()
        return "\n".join(linhas)
    return _montar(partes, corpos).rstrip("\n")

def otimizar_alvo(ir_texto):
    #Escolhe o alvo e aplica o passe
    partes, glob = _segmentar(ir_texto)
    funcs = [p for p in partes if p[0] == "func" and p[3] is not None]
    escolhido = None
    for nome, detecta, aplica in ALVOS:
        try:
            if any(detecta(f[3], glob) for f in funcs):
                escolhido = (nome, aplica)
                break
        except (KeyError, RuntimeError):
            continue
    ref = _aplicar(partes, glob, lambda f, g: f)
    if escolhido is None:
        return {"alvo": "nenhuma oportunidade de otimização encontrada",
                "classe": "nenhuma", "ref": _corpo_exibicao(partes, ref, True),
                "opt": _corpo_exibicao(partes, ref, False), "extras": [],
                "aviso": False, "titulo": "IR otimizada esperada (-O1), equivalente:"}
    classe, aplica = escolhido
    registro = []
    if classe == "licm":
        corpos = _aplicar(partes, glob,
                          lambda f, g: compactar_temps(_loop_licm(f, g, registro)))
    else:
        corpos = _aplicar(partes, glob, aplica)
    extras, aviso = [], False
    titulo = "IR otimizada esperada (-O1), equivalente:"
    orig_txt = "\n".join(sum((p[4] for p in partes if p[0] == "func"), []))
    opt_txt = "\n".join(sum((c for c in corpos), []))
    visiveis = lambda t: {x for x in re.findall(r"[A-Za-z_]\w*(?:#\w+)?", t)
                          if not RE_TEMP.match(x)}
    if classe == "inalcancavel":
        alvo, aviso = "eliminação de código morto/inalcançável", True
        titulo = "IR otimizada esperada (-O1):"
    elif classe == "licm":
        alvo = "code motion de expressão loop-invariant"
        ops = []
        for ins in registro:
            ops += [o for o in usos(ins) if o not in ops]
        if ops:
            verbo = "são modificados" if len(ops) > 1 else "é modificado"
            extras.append(f"Observação: mover antes do laço é seguro porque "
                          f"{_juntar(ops)} não {verbo} no laço.")
    elif classe == "forca":
        alvo = ("redução de força / atualização incremental de expressão "
                "relacionada à indução")
        titulo = "IR otimizada possível (-O1), equivalente:"
        extras.append("Critério: validar equivalência para cada iteração; "
                      "diferentes implementações podem preferir não aplicar "
                      "esta otimização. Não exigir forma textual idêntica.")
    elif classe == "cse":
        alvo = "eliminação de subexpressão comum local"
    elif classe == "algebra":
        alvo = "simplificação algébrica e propagação de cópias"
    elif classe == "copias":
        alvo = "eliminação de cópias"
    elif classe == "morta":
        alvo = "eliminação de atribuição morta"
        sumiram = [v for v in sorted(visiveis(orig_txt) - visiveis(opt_txt))
                   if v not in ("func", "print", "return", "goto", "ifFalse",
                                "param", "call", "read", "load", "store", "not",
                                "and", "or", "true", "false", "endfunc")
                   and not v.startswith("L_")]
        if sumiram:
            extras.append(f"Verificar: cálculo/definição de {_juntar(sumiram)} "
                          "não tem uso observável e pode ser removido.")
    elif classe == "constantes":
        antes_if = sum(1 for p in funcs for i in p[3] if i[0] == "ifFalse")
        depois_if = sum(1 for t in corpos for l in t if l.strip().startswith("ifFalse"))
        literal_literal = any(i[0] == "bin" and eh_lit(i[2]) and eh_lit(i[4])
                              for p in funcs for i in p[3])
        if depois_if < antes_if:
            alvo = "dobra de condição constante + eliminação de código inalcançável"
        elif literal_literal:
            alvo = "propagação de constantes + simplificação/dobra algébrica"
            prints = [l.strip()[6:] for t in corpos for l in t
                      if l.strip().startswith("print ")]
            if len(prints) == 1 and eh_lit(prints[0]):
                extras.append("Verificar: remoção de temporários/atribuições "
                              "tornou-se possível; resultado observável "
                              f"permanece {prints[0]}.")
        else:
            alvo = "reaching definitions/propagação global de constantes e dobra"
    else:
        alvo = "otimizações gerais (constantes, cópias, subexpressões, código morto)"
    #Apresentação da referência
    if classe == "inalcancavel":
        sel = [(p, c) for p, c in zip([q for q in partes if q[0] == "func"], ref)
               if p[3] is not None and _tem_inalcancavel(p[3])]
        ref_txt = _montar([p for p, _ in sel], [c for _, c in sel]).rstrip("\n")
    else:
        if any(e.startswith("Verificar") for e in extras):
            #Referência no nível dos comandos
            ref = _aplicar(partes, glob, lambda f, g: fundir_temporarios(f, g))
        ref_txt = _corpo_exibicao(partes, ref, True)
        if classe in ("licm", "forca"):
            #Referência de laço
            linhas = ref_txt.split("\n")
            pos = {l.strip(): n for n, l in enumerate(linhas) if l.endswith(":")}
            volta = [n for n, l in enumerate(linhas)
                     if l.strip().startswith("goto ")
                     and pos.get(l.strip()[5:] + ":", n + 1) < n]
            if volta:
                ref_txt = "\n".join(linhas[:volta[-1] + 1])
    opt_txt_exib = _corpo_exibicao(partes, corpos, False)
    if classe == "inalcancavel":
        saida = []
        for l in opt_txt_exib.split("\n"):
            if re.match(r"^\s*(?:\S+ = )?call \S+, 0$", l):
                saida.append("  param")
            saida.append(l)
        opt_txt_exib = "\n".join(saida)
    return {"alvo": alvo, "classe": classe, "aviso": aviso, "titulo": titulo,
            "ref": ref_txt,
            "opt": opt_txt_exib, "extras": extras}
