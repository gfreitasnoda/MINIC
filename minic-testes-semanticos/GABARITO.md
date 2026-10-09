# Gabarito — suíte de testes semânticos MINIC

## Convenções e compatibilidade

Os 20 programas usam as construções presentes na gramática entregue (`int`, `float`, `bool`, `void`, funções, blocos, `if/else`, `while`, vetores, chamadas e operadores). Não usam comentários, literais `true/false`, `break` nem `continue`, pois esses itens não aparecem no lexer/parser concretos examinados.

**Importante:** o material de projeto disponível contém o parser sintático, mas não uma implementação de analisador semântico. Portanto, as mensagens abaixo são o **gabarito de referência completo** conforme as regras da apostila da Aula 15 — não uma transcrição garantida da saída de um executável existente. Para tornar testes automatizáveis, o gabarito define códigos, posições e texto canônico. A política supõe escopo léxico com sombreamento, coleta de assinaturas para recursão, promoção `int → float` e proibição de estreitamento implícito.

Formato de sucesso esperado: `Análise semântica concluída: 0 erros; programa aceito.` Em caso de erro, listam-se todos os diagnósticos semânticos independentes em ordem de origem; a última linha informa a contagem e rejeição. Colunas são 1-based.

## Testes de aceitação

### `01_promocao_numerica.c` — ACEITO

Conversões int→float nos dois argumentos de soma. O retorno permanece int e é compatível com principal. A variável r é float.

**Saída/gabarito:** `Análise semântica concluída: 0 erros; programa aceito.`

### `02_sombreamento_lexico.c` — ACEITO

O x float do bloco interno sombreia temporariamente o x int externo. Ao fechar o bloco, a resolução volta ao símbolo externo; x + 1 é int.

**Saída/gabarito:** `Análise semântica concluída: 0 erros; programa aceito.`

### `03_logica_e_comparacoes.c` — ACEITO

< e == produzem bool; ! e && recebem bool. menor retorna bool, e a condição if(ok) está tipada corretamente.

**Saída/gabarito:** `Análise semântica concluída: 0 erros; programa aceito.`

### `04_vetores_e_parametro_array.c` — ACEITO

dados é parâmetro array de int; i é int e o acesso tem tipo int. A soma promove int para float; xs é passado ao parâmetro array de int.

**Saída/gabarito:** `Análise semântica concluída: 0 erros; programa aceito.`

### `05_lacos_aninhados.c` — ACEITO

As duas condições de while são comparações int que resultam em bool. Escopos, atribuições e expressões aritméticas são compatíveis; retorno int.

**Saída/gabarito:** `Análise semântica concluída: 0 erros; programa aceito.`

### `06_recursao_e_retorno.c` — ACEITO

A assinatura de fatorial deve estar disponível ao analisar o próprio corpo. A chamada recursiva recebe int, ambos os ramos retornam int, e principal retorna int.

**Saída/gabarito:** `Análise semântica concluída: 0 erros; programa aceito.`

### `07_void_como_comando.c` — ACEITO

inicializa(v) é chamada sem valor em posição de comando; return sem expressão é válido em função void. O índice é int e elemento int.

**Saída/gabarito:** `Análise semântica concluída: 0 erros; programa aceito.`

### `08_retornos_nos_dois_ramos.c` — ACEITO

A comparação n < 0 é bool. Os dois ramos do if/else retornam int, portanto não há caminho de queda; chamada retorna int.

**Saída/gabarito:** `Análise semântica concluída: 0 erros; programa aceito.`

### `09_atribuicao_associativa.c` — ACEITO

A atribuição é associativa à direita: b = 3 e depois a recebe o resultado compatível. Ambas são variáveis int atribuíveis.

**Saída/gabarito:** `Análise semântica concluída: 0 erros; programa aceito.`

### `10_vetor_bool_e_funcoes.c` — ACEITO

A comparação produz bool para os retornos de contem; o parâmetro vetor recebe valores[2] de tipo array int. O if/else de principal cobre ambos os retornos.

**Saída/gabarito:** `Análise semântica concluída: 0 erros; programa aceito.`

## Testes de rejeição

### `11_estreitamento_implicito.c` — REJEITADO

Diagnósticos esperados:

```text
SEM003 — linha 3, coluna 9: Não é possível atribuir float a int sem conversão permitida (destino “n”; expressão “2.5”).
Análise semântica concluída: 1 erro; programa rejeitado.
```

### `12_identificador_nao_declarado.c` — REJEITADO

Diagnósticos esperados:

```text
SEM001 — linha 3, coluna 9: Identificador “ausente” não declarado neste escopo.
Análise semântica concluída: 1 erro; programa rejeitado.
```

### `13_declaracao_duplicada.c` — REJEITADO

Diagnósticos esperados:

```text
SEM002 — linha 3, coluna 11: “medida” já declarado neste escopo; declaração anterior na linha 2, coluna 9 (tipo int).
Análise semântica concluída: 1 erro; programa rejeitado.
```

### `14_condicao_nao_booleana.c` — REJEITADO

Diagnósticos esperados:

```text
SEM005 — linha 4, coluna 12: Condição de while deve ter tipo bool; recebeu int (expressão “n”).
Análise semântica concluída: 1 erro; programa rejeitado.
```

### `15_indice_float.c` — REJEITADO

Diagnósticos esperados:

```text
SEM006 — linha 3, coluna 11: Índice do vetor “dados” deve ser int; recebeu float (expressão “1.0”).
Análise semântica concluída: 1 erro; programa rejeitado.
```

### `16_aridade_incorreta.c` — REJEITADO

Diagnósticos esperados:

```text
SEM007 — linha 5, coluna 12: “combina” espera 2 argumentos, mas recebeu 1.
Análise semântica concluída: 1 erro; programa rejeitado.
```

### `17_tipos_de_argumentos.c` — REJEITADO

Diagnósticos esperados:

```text
SEM008 — linha 5, coluna 20: Argumento 1 de “combina”: esperado int, recebido bool (expressão “1 < 2”).
SEM008 — linha 5, coluna 27: Argumento 2 de “combina”: esperado float, recebido bool (expressão “3 < 4”).
Análise semântica concluída: 2 erros; programa rejeitado.
```

### `18_uso_de_void_como_valor.c` — REJEITADO

Diagnósticos esperados:

```text
SEM012 — linha 6, coluna 9: Função “acao” não produz valor (retorno void) e não pode ser usada como expressão de atribuição.
Análise semântica concluída: 1 erro; programa rejeitado.
```

### `19_retorno_e_cobertura.c` — REJEITADO

Diagnósticos esperados:

```text
SEM009 — linha 2, coluna 12: Retorno float incompatível com o tipo int da função “fracionario”; conversão implícita de float para int não permitida.
SEM011 — linha 4, coluna 1: A função “incompleta” pode terminar sem retornar int; o ramo em que “condicao” é falso alcança o fim do corpo.
Análise semântica concluída: 2 erros; programa rejeitado.
```

### `20_destino_nao_atribuivel.c` — REJEITADO

Diagnósticos esperados:

```text
SEM013 — linha 3, coluna 5: Destino de atribuição não é atribuível; o literal inteiro “3” não designa uma variável ou elemento de vetor.
Análise semântica concluída: 1 erro; programa rejeitado.
```

## Catálogo de códigos usado

- **SEM001** identificador não declarado; **SEM002** declaração duplicada; **SEM003** incompatibilidade de atribuição/conversão.
- **SEM005** condição não booleana; **SEM006** índice de vetor inválido; **SEM007** aridade incorreta; **SEM008** tipo de argumento incompatível.
- **SEM009** retorno incompatível; **SEM011** possível queda de função não `void`; **SEM012** chamada `void` usada como valor; **SEM013** destino não atribuível.

Os números mantêm os códigos descritos na apostila quando disponíveis; **SEM013** nomeia explicitamente a regra de valor-L necessária para este caso. Diagnósticos sintáticos ou lexicais não pertencem a este gabarito.
