#Uso: ./testes_semanticos_py.sh analisador_semantico.py minic-testes-semanticos

#!/usr/bin/env bash
set -uo pipefail

if (($# != 2)); then
  echo "Uso: $0 [arquivo_fonte] [diretorio]" >&2
  exit 2
fi

arquivo_fonte=$1
diretorio=$2

if [[ ! -f "$arquivo_fonte" ]]; then
  echo "Erro: arquivo-fonte não encontrado: $arquivo_fonte" >&2
  exit 2
fi

if [[ ! -d "$diretorio" ]]; then
  echo "Erro: diretório de testes não encontrado: $diretorio" >&2
  exit 2
fi

if ! command -v python >/dev/null 2>&1; then
  echo "Erro: python não está disponível." >&2
  exit 2
fi

tmp_dir=$(mktemp -d) || exit 2
trap 'rm -rf "$tmp_dir"' EXIT

total=0
aprovados=0
reprovados=0

while IFS= read -r -d '' teste; do
  total=$((total + 1))
  gabarito="${teste%.c}.gabarito"
  saida="$tmp_dir/saida-$total.txt"

  if [[ ! -f "$gabarito" ]]; then
    printf '[FALHA] %s — gabarito não encontrado\n' "$teste"
    reprovados=$((reprovados + 1))
    continue
  fi

  python "$arquivo_fonte" "$teste" >"$saida" 2>&1 || true

  if diff -u -- "$gabarito" "$saida" >"$tmp_dir/diff-$total.txt"; then
    printf '[OK]     %s\n' "$teste"
    aprovados=$((aprovados + 1))
  else
    printf '[FALHA] %s\n' "$teste"
    cat "$tmp_dir/diff-$total.txt"
    reprovados=$((reprovados + 1))
  fi
done < <(find "$diretorio" -type f -name '*.c' -print0 | sort -z)

if ((total == 0)); then
  echo "Erro: nenhum arquivo .c encontrado em $diretorio" >&2
  exit 2
fi

printf '\nResultado: %d/%d aprovados; %d reprovados.\n' \
  "$aprovados" "$total" "$reprovados"

if ((reprovados == 0)); then
  exit 0
else
  exit 1
fi
