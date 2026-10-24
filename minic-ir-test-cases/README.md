# Suíte de 30 casos MINIC para avaliação da IR

## Objetivo
Conjunto independente para avaliar reconhecimento semântico, geração de IR de três endereços e otimizações antes da geração de assembly. Cada caso contém um programa-fonte `.c` e um `esperado.txt` com resultado/critério de comparação.

## Composição
- `testes/01-validos-sem-otimizacao/`: 10 programas válidos para referência em modo `-O0`.
- `testes/02-erros/`: 10 programas inválidos com categoria e critério diagnóstico esperado.
- `testes/03-otimizacoes/`: 10 programas válidos com IR de referência e transformação esperada no modo `-O1`.

## Importante sobre os gabaritos
A especificação MINIC define IR de três endereços e operações mínimas, mas não fixa uma serialização textual, política de nomes temporários, detalhes de chamadas nem forma de endereçamento de vetores. Portanto, os gabaritos válidos são uma **IR canônica de referência**, não uma exigência de igualdade byte a byte. Compare a estrutura, fluxo de controle, operandos, efeitos observáveis e semântica; normalize nomes de temporários e rótulos. Para erros, compare rejeição e categoria, sem exigir redação idêntica do diagnóstico.

As saídas otimizadas descrevem transformações-alvo. Para O09, a redução de força é uma otimização possível e não deve ser exigida de um otimizador que não a implemente; ainda assim, o programa deve compilar e produzir comportamento equivalente. A validação deve sempre confrontar execução/semântica de `-O0` e `-O1` para entradas relevantes.

## Execução sugerida
Para cada `.c` válido, execute a opção equivalente a `minic --ir arquivo.c` no modo sem otimização, depois `minic -O1 --ir arquivo.c` nos casos de otimização. Para inválidos, execute a compilação/análise e confirme rejeição e classe do erro. Os nomes exatos das opções podem variar conforme o compilador; a especificação MINIC sugere `--ir`, `-O0` e `-O1`.

Não há executável do compilador incluído neste pacote. A suíte fornece programas de entrada e gabaritos/critério de avaliação, não afirma que foi executada contra uma implementação específica.
