#!/usr/bin/env bash
set -u

uso() {
  echo "Uso: $0 caminho/minic.py diretorio_dos_testes" >&2
  echo "Exemplo: $0 ./minic.py ./testes_ir" >&2
  exit 2
}

[[ $# -eq 2 ]] || uso

MINIC_PY="$1"
SUITE="${2%/}"
PYTHON_BIN="${PYTHON_BIN:-python3}"

[[ -f "$MINIC_PY" ]] || {
  echo "Erro: não encontrei o compilador: $MINIC_PY" >&2
  exit 2
}
[[ -d "$SUITE" ]] || {
  echo "Erro: não encontrei o diretório de testes: $SUITE" >&2
  exit 2
}
command -v "$PYTHON_BIN" >/dev/null 2>&1 || {
  echo "Erro: Python não encontrado: $PYTHON_BIN" >&2
  exit 2
}

# Personalize com, por exemplo:
# IGNORE_RE='^[[:space:]]*(mensagem|message|diagnóstico|Resultado|Modo|IR):'
IGNORE_RE="${IGNORE_RE:-^[[:space:]]*(mensagem|message|erro|error|aviso|warning)([[:space:]:-]|$)}"

tmp_dir="$(mktemp -d)" || {
  echo "Erro: não foi possível criar a área temporária." >&2
  exit 2
}
trap 'rm -rf "$tmp_dir"' EXIT

declare -A total=( [normais]=0 [erros]=0 [otimizacao]=0 )
declare -A aprovados=( [normais]=0 [erros]=0 [otimizacao]=0 )

classificar() {
  local nome
  nome="$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')"

  case "$nome" in
    *03-*)
      printf 'otimizacao'
      ;;
    *02-*)
      printf 'erros'
      ;;
    *01-*)
      printf 'normais'
      ;;
    *)
      return 1
      ;;
  esac
}

encontrar_gabarito() {
  local base="$1"
  local extensao=".txt"

  for extensao in gabarito expected out txt; do
    if [[ -f "${base}.${extensao}" ]]; then
      printf '%s' "${base}.${extensao}"
      return 0
    fi
  done
  return 1
}

filtrar_mensagens() {
  local origem="$1"
  local destino="$2"
  local status

  grep -Eiv -- "$IGNORE_RE" "$origem" > "$destino"
  status=$?

  # grep retorna 1 quando todas as linhas foram filtradas; isso é válido.
  (( status == 0 || status == 1 ))
}

encontrou_teste=0

while IFS= read -r -d '' fonte; do
  encontrou_teste=1
  relativo="${fonte#"$SUITE"/}"

  categoria="$(classificar "$relativo")" || {
    echo "Erro: não foi possível identificar a categoria de: $relativo" >&2
    echo "Use nomes de pasta ou arquivo contendo normal, erro ou otimizacao." >&2
    exit 2
  }

  total["$categoria"]=$(( total["$categoria"] + 1 ))
  numero="${total[$categoria]}"
  nome="$(basename "$fonte")"
  gabarito="$(encontrar_gabarito "${fonte%.c}")" || {
    echo "[FALHA][$categoria] $relativo — gabarito não encontrado"
    continue
  }

  saida="$tmp_dir/saida.txt"
  saida_filtrada="$tmp_dir/saida_filtrada.txt"
  gabarito_filtrado="$tmp_dir/gabarito_filtrado.txt"

  "$PYTHON_BIN" "$MINIC_PY" "$fonte" > "$saida" 2>&1
  codigo_saida=$?

  if ! filtrar_mensagens "$saida" "$saida_filtrada" ||
     ! filtrar_mensagens "$gabarito" "$gabarito_filtrado"; then
    echo "[FALHA][$categoria] $nome — não foi possível processar os resultados"
    continue
  fi

  resultado="OK"
  motivo=""

  if [[ "$categoria" == "erros" ]]; then
    if (( codigo_saida == 0 )); then
      resultado="FALHA"
      motivo="o compilador aceitou um caso que deveria apresentar erro"
    fi
  elif (( codigo_saida != 0 )); then
    resultado="FALHA"
    motivo="o compilador retornou código de saída $codigo_saida"
  fi

  if [[ "$resultado" == "OK" ]] &&
     ! cmp -s "$gabarito_filtrado" "$saida_filtrada"; then
    resultado="FALHA"
    motivo="resultado diferente do gabarito após ignorar as mensagens"
  fi

  if [[ "$resultado" == "OK" ]]; then
    aprovados["$categoria"]=$(( aprovados["$categoria"] + 1 ))
    echo "[OK][$categoria] $nome"
  else
    echo "[$resultado][$categoria] $nome — $motivo"
  fi
done < <(find "$SUITE" -type f -name '*.c' -print0 | sort -z)

if (( encontrou_teste == 0 )); then
  echo "Erro: nenhum arquivo .c foi encontrado em $SUITE" >&2
  exit 2
fi

echo
echo "Resumo:"
echo "  Testes normais:      ${aprovados[normais]}/${total[normais]} aprovados"
echo "  Testes com erro:     ${aprovados[erros]}/${total[erros]} aprovados"
echo "  Testes otimização:  ${aprovados[otimizacao]}/${total[otimizacao]} aprovados"

reprovados=$(( total[normais] - aprovados[normais] \
             + total[erros] - aprovados[erros] \
             + total[otimizacao] - aprovados[otimizacao] ))

if (( reprovados == 0 )); then
  exit 0
else
  exit 1
fi
