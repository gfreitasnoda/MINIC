#!/usr/bin/env bash
set -u

uso() {
  printf 'Uso: %s arquivo_fonte.c diretorio_dos_testes\n' "$0" >&2
  printf 'Exemplo: %s ./minic.c ./testes_ir\n' "$0" >&2
  exit 2
}
[[ $# -eq 2 ]] || uso

FONTE="$1"
SUITE="${2%/}"
CC="${CC:-gcc}"
# Use CFLAGS para personalizar as opções, por exemplo: CFLAGS='-std=c11 -Wall -Wextra -O2'
CFLAGS="${CFLAGS:--Wall -Wextra -O2}"

[[ -f "$FONTE" ]] || { printf 'Erro: fonte C não encontrada: %s\n' "$FONTE" >&2; exit 2; }
[[ -d "$SUITE" ]] || { printf 'Erro: diretório de testes não encontrado: %s\n' "$SUITE" >&2; exit 2; }
command -v "$CC" >/dev/null 2>&1 || { printf 'Erro: compilador C não encontrado: %s\n' "$CC" >&2; exit 2; }

TMP_DIR="$(mktemp -d)" || { printf 'Erro: não foi possível criar a área temporária.\n' >&2; exit 2; }
trap 'rm -rf "$TMP_DIR"' EXIT
EXECUTAVEL="$TMP_DIR/minic"
# Compila uma única vez; a divisão de CFLAGS em opções é intencional.
# Executa o binário passando cada arquivo de teste como argumento.
if ! "$CC" $CFLAGS "$FONTE" -o "$EXECUTAVEL"; then
  printf 'Erro: falha ao compilar o MINIC em C.\n' >&2
  exit 2
fi

# Ajustável para os prefixos usados pelo compilador.
IGNORE_RE="${IGNORE_RE:-^[[:space:]]*(mensagem|message|diagnóstico|diagnostico|erro|error|aviso|warning)([[:space:]:-]|$)}"
declare -A total=( [normais]=0 [erros]=0 [otimizacao]=0 )
declare -A aprovados=( [normais]=0 [erros]=0 [otimizacao]=0 )

classificar() {
  local nome
  nome="$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')"
  case "$nome" in
    *03-*) printf 'otimizacao' ;;
    *02-*) printf 'erros' ;;
    *01-*) printf 'normais' ;;
    *) return 1 ;;
  esac
}
localizar_gabarito() {
  local base="$1" extensao
  for extensao in gabarito expected out txt; do
    if [[ -f "${base}.${extensao}" ]]; then printf '%s' "${base}.${extensao}"; return 0; fi
  done
  return 1
}
filtrar_mensagens() {
  local origem="$1" destino="$2" status
  grep -Eiv -- "$IGNORE_RE" "$origem" > "$destino"
  status=$?
  (( status == 0 || status == 1 ))
}

contagem=0
while IFS= read -r -d '' fonte_teste; do
  contagem=$((contagem + 1))
  relativo="${fonte_teste#"$SUITE"/}"
  if ! categoria="$(classificar "$relativo")"; then
    printf 'Erro: categoria não identificada para %s. Use nomes contendo normal, erro ou otimizacao.\n' "$relativo" >&2
    exit 2
  fi
  total["$categoria"]=$(( total["$categoria"] + 1 ))
  nome="$(basename "$fonte_teste")"
  if ! gabarito="$(localizar_gabarito "${fonte_teste%.c}")"; then
    printf '[FALHA][%s] %s — gabarito não encontrado\n' "$categoria" "$nome"
    continue
  fi
  saida="$TMP_DIR/saida.txt"
  saida_filtrada="$TMP_DIR/saida_filtrada.txt"
  gabarito_filtrado="$TMP_DIR/gabarito_filtrado.txt"
  "$EXECUTAVEL" "$fonte_teste" > "$saida" 2>&1
  codigo_saida=$?
  if ! filtrar_mensagens "$saida" "$saida_filtrada" || ! filtrar_mensagens "$gabarito" "$gabarito_filtrado"; then
    printf '[FALHA][%s] %s — não foi possível processar os resultados\n' "$categoria" "$nome"
    continue
  fi
  resultado='OK'
  motivo=''
  if [[ "$categoria" == 'erros' ]]; then
    if (( codigo_saida == 0 )); then resultado='FALHA'; motivo='o compilador aceitou um caso que deveria apresentar erro'; fi
  elif (( codigo_saida != 0 )); then
    resultado='FALHA'; motivo="o compilador retornou código de saída $codigo_saida"
  fi
  if [[ "$resultado" == 'OK' ]] && ! cmp -s "$gabarito_filtrado" "$saida_filtrada"; then
    resultado='FALHA'; motivo='resultado diferente do gabarito após ignorar as mensagens'
  fi
  if [[ "$resultado" == 'OK' ]]; then
    aprovados["$categoria"]=$(( aprovados["$categoria"] + 1 ))
    printf '[OK][%s] %s\n' "$categoria" "$nome"
  else
    printf '[FALHA][%s] %s — %s\n' "$categoria" "$nome" "$motivo"
  fi
done < <(find "$SUITE" -type f -name '*.c' -print0 | sort -z)

if (( contagem == 0 )); then printf 'Erro: nenhum arquivo .c foi encontrado em %s\n' "$SUITE" >&2; exit 2; fi
printf '\nResumo:\n'
printf '  Testes normais:      %d/%d aprovados\n' "${aprovados[normais]}" "${total[normais]}"
printf '  Testes com erro:     %d/%d aprovados\n' "${aprovados[erros]}" "${total[erros]}"
printf '  Testes otimização:   %d/%d aprovados\n' "${aprovados[otimizacao]}" "${total[otimizacao]}"
reprovados=$(( total[normais] - aprovados[normais] + total[erros] - aprovados[erros] + total[otimizacao] - aprovados[otimizacao] ))
(( reprovados == 0 )) && exit 0 || exit 1
